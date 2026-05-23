/*
 * X-Bit (Xbit) Modchip Flasher
 *
 * Cross-platform tool to read, write, verify and format the X-BIT modchip
 * for the Original Xbox.  Requires hidapi.
 *
 * Usage:  xbit_flasher <mode> <layout> [bank] [file]
 *   mode:   r=read  w=write  v=verify  f=format
 *   layout: 1-6  (see PrintMemoryBankLayout)
 *   bank:   1-6  (not required for format)
 */

#ifdef _WIN32
#  include <windows.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <vector>

#ifndef _WIN32
#  include <unistd.h>   /* usleep, sleep */
#endif

#include "xbit.h"

/* ── Byte-swap helpers (8051 is big-endian, host is little-endian) ──────────*/
static inline uint16 swap16(uint16 x)
{
	return (uint16)(((x & 0xFF00u) >> 8) | ((x & 0x00FFu) << 8));
}

/* ── Portable sleep ─────────────────────────────────────────────────────────*/
static inline void sleep_ms(unsigned ms)
{
#ifdef _WIN32
	Sleep(ms);
#else
	usleep((useconds_t)ms * 1000u);
#endif
}

/* ── Debug logging ──────────────────────────────────────────────────────────*/
#ifdef XBIT_DEBUG
static void dbg_print_bytes(const PREPORT_BUF buf, int length)
{
	printf("[DBG] reportID=%u cmd=%02X buf:", buf->reportID, buf->report.u.cmd);
	for (int i = 0; i < length; ++i)
		printf(" %02X", buf->report.u.buffer[i]);
	printf("\n");
}
#  define DBG_BYTES(buf, len) dbg_print_bytes((buf), (len))
#  define DBG(fmt, ...)       printf("[DBG] " fmt "\n", ##__VA_ARGS__)
#else
#  define DBG_BYTES(buf, len) ((void)0)
#  define DBG(fmt, ...)       ((void)0)
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * XbitFlasher — constructor / destructor
 * ═══════════════════════════════════════════════════════════════════════════*/

XbitFlasher::XbitFlasher()
	: memory_layout_id(0)
	, handle(nullptr)
	, device_initialized(false)
{
	memset(&statusBuf, 0, sizeof(statusBuf));
	hid_init();
}

XbitFlasher::~XbitFlasher()
{
	if (device_initialized)
		CloseDevice();
	hid_exit();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Device open / close
 * ═══════════════════════════════════════════════════════════════════════════*/

bool XbitFlasher::OpenDevice()
{
	wchar_t wstr[256];

	handle = hid_open(ST_VENDOR_ID, ST_PRODUCT_ID, nullptr);
	if (!handle) {
		fprintf(stderr, "ERROR: Failed to open HID device (VID=%04X PID=%04X).\n"
		                "       Is the X-BIT plugged in?  On Linux, check udev rules.\n",
		        ST_VENDOR_ID, ST_PRODUCT_ID);
		return false;
	}

	/* Validate manufacturer string */
	if (hid_get_manufacturer_string(handle, wstr, 256) < 0 ||
	    wcsncmp(wstr, DEVICE_MFG, wcslen(DEVICE_MFG)) != 0) {
		fprintf(stderr, "ERROR: Unexpected manufacturer string: %ls\n"
		                "       Expected: %ls\n"
		                "       Try replugging the USB cable and retrying.\n",
		        wstr, DEVICE_MFG);
		CloseDevice();
		return false;
	}

	/* Validate product string */
	if (hid_get_product_string(handle, wstr, 256) < 0 ||
	    wcsncmp(wstr, DEVICE_PRODUCT, wcslen(DEVICE_PRODUCT)) != 0) {
		fprintf(stderr, "ERROR: Unexpected product string: %ls\n"
		                "       Expected: %ls\n",
		        wstr, DEVICE_PRODUCT);
		CloseDevice();
		return false;
	}

	if (!GetStatus()) {
		fprintf(stderr, "ERROR: Failed to get initial status from modchip.\n"
		                "       Try replugging the USB cable and retrying.\n");
		CloseDevice();
		return false;
	}

	memory_layout_id = (int)GetMemoryLayout();
	device_initialized = true;
	printf("Device opened. Memory layout: %d\n", memory_layout_id);
	return true;
}

bool XbitFlasher::CloseDevice()
{
	if (handle) {
		Reset();
		hid_close(handle);
		handle = nullptr;
	}
	device_initialized = false;
	return true;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Device state queries
 * ═══════════════════════════════════════════════════════════════════════════*/

bool XbitFlasher::IsDeviceInitialized() const
{
	return device_initialized;
}

bool XbitFlasher::IsValidStatus() const
{
	return (statusBuf.reportID == 0 &&
	        statusBuf.report.u.status.cmd == CMD_GET_STATUS);
}

uchar XbitFlasher::GetCurrentCommand() const
{
	return statusBuf.report.u.status.currentCmd;
}

uchar XbitFlasher::GetMemoryLayout() const
{
	return statusBuf.report.u.status.page;
}

uchar XbitFlasher::GetVMState() const
{
	return statusBuf.report.u.status.vm;
}

bool XbitFlasher::IsDeviceReady() const
{
	return (GetCurrentCommand() == 0);
}

bool XbitFlasher::IsDeviceBusFree() const
{
	return (GetVMState() & STATUS_BUS_FREE) != 0;
}

bool XbitFlasher::IsDeviceBusAttached() const
{
	return (GetVMState() & STATUS_BUS_ATTACHED) != 0;
}

bool XbitFlasher::IsDeviceWriteProtected() const
{
	return (GetVMState() & STATUS_WRITE_PROTECT) != 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Low-level HID I/O
 * ═══════════════════════════════════════════════════════════════════════════*/

int XbitFlasher::InternalRead(PREPORT_BUF output)
{
	/* NOTE: Must use hid_get_feature_report, NOT hid_read */
	int res = hid_get_feature_report(handle,
	                                 (unsigned char *)output,
	                                 sizeof(REPORT_BUF));
	if (res == (int)sizeof(REPORT_BUF))
		DBG_BYTES(output, OUTPUT_REPORT_SIZE);
	return res;
}

int XbitFlasher::InternalWrite(PREPORT_BUF input)
{
	int res = hid_write(handle, (unsigned char *)input, sizeof(REPORT_BUF));
	/* The chip needs ~8 ms to process a command before we read back */
	sleep_ms(8);
	if (res == (int)sizeof(REPORT_BUF))
		DBG_BYTES(input, OUTPUT_REPORT_SIZE);
	return res;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Protocol commands
 * ═══════════════════════════════════════════════════════════════════════════*/

bool XbitFlasher::GetStatus()
{
	REPORT_BUF buf;
	memset(&buf, 0, sizeof(buf));
	buf.report.u.cmd = CMD_GET_STATUS;

	if (InternalWrite(&buf) != (int)sizeof(REPORT_BUF)) {
		fprintf(stderr, "ERROR: Failed to send CMD_GET_STATUS.\n");
		return false;
	}
	memset(&statusBuf, 0, sizeof(statusBuf));
	if (InternalRead(&statusBuf) != (int)sizeof(REPORT_BUF)) {
		fprintf(stderr, "ERROR: Failed to read CMD_GET_STATUS reply.\n");
		return false;
	}
	return true;
}

bool XbitFlasher::Reset()
{
	REPORT_BUF buf;
	memset(&buf, 0, sizeof(buf));
	buf.report.u.cmd = CMD_RESET;

	if (InternalWrite(&buf) != (int)sizeof(REPORT_BUF)) {
		fprintf(stderr, "ERROR: Failed to send CMD_RESET.\n");
		return false;
	}
	return true;
}

bool XbitFlasher::SetVM(uchar vm)
{
	REPORT_BUF buf;
	memset(&buf, 0, sizeof(buf));
	buf.report.u.setRegs.cmd = CMD_SET_VM;
	buf.report.u.setRegs.vm  = vm;

	if (InternalWrite(&buf) != (int)sizeof(REPORT_BUF)) {
		fprintf(stderr, "ERROR: Failed to send CMD_SET_VM.\n");
		return false;
	}
	return true;
}

bool XbitFlasher::GetBus()   { return SetVM(1); }
bool XbitFlasher::ReleaseBus() { return SetVM(0); }

bool XbitFlasher::SetPage(int layout_id)
{
	REPORT_BUF buf;
	memset(&buf, 0, sizeof(buf));
	buf.report.u.setRegs.cmd  = CMD_SET_PAGE;
	buf.report.u.setRegs.page = (uchar)(layout_id & 0xFF);

	if (InternalWrite(&buf) != (int)sizeof(REPORT_BUF)) {
		fprintf(stderr, "ERROR: Failed to send CMD_SET_PAGE.\n");
		return false;
	}
	return true;
}

/* ───────────────────────────────────────────────────────────────────────────
 * ReadFlash / WriteFlash / EraseBlock
 *
 * XBIT address model (differs from original DK3200):
 *   - rw.flash   = sector number (not PRIMARY/SECONDARY_FLASH)
 *   - rw.address = byte offset within that sector (big-endian)
 * ───────────────────────────────────────────────────────────────────────────*/

bool XbitFlasher::ReadFlash(uchar sector, uint16 offset,
                             uchar *buffer, uint16 nBytes)
{
	if (!nBytes) {
		fprintf(stderr, "ERROR: ReadFlash called with nBytes=0.\n");
		return false;
	}

	REPORT_BUF buf;
	memset(&buf, 0, sizeof(buf));
	buf.report.u.cmd        = CMD_READ;
	buf.report.u.rw.flash   = sector;
	buf.report.u.rw.address = swap16(offset);
	buf.report.u.rw.nBytes  = swap16(nBytes);

	if (InternalWrite(&buf) != (int)sizeof(REPORT_BUF)) {
		fprintf(stderr, "ERROR: Failed to send CMD_READ.\n");
		return false;
	}

	time_t t_start = time(nullptr);

	uint16 remaining = nBytes;
	uint16 reported  = remaining;
	while (remaining) {
		if (InternalRead(&buf) != (int)sizeof(REPORT_BUF)) {
			fprintf(stderr, "ERROR: Failed to read CMD_READ reply.\n");
			return false;
		}
		/* buffer[0] is the command echo byte — skip it */
		uint16 chunk = (remaining < (uint16)(CMD_SIZE - 1))
		               ? remaining : (uint16)(CMD_SIZE - 1);
		memcpy(buffer, buf.report.u.buffer + 1, chunk);
		buffer    += chunk;
		remaining -= chunk;

		/* Progress: print on every 100-byte boundary */
		if ((reported / 100) != (remaining / 100)) {
			printf("  Reading: %5u bytes remaining...\n", remaining);
			reported = remaining;
		}
	}

	double elapsed = difftime(time(nullptr), t_start);
	printf("  Read complete in %.0f second(s).\n", elapsed);
	return true;
}

bool XbitFlasher::WriteFlash(uchar sector, uint16 offset,
                              const uchar *buffer, uint16 nBytes)
{
	if (!nBytes) {
		fprintf(stderr, "ERROR: WriteFlash called with nBytes=0.\n");
		return false;
	}

	/* Compute checksum before sending */
	uchar checksum = 0;
	for (uint16 i = 0; i < nBytes; ++i)
		checksum += buffer[i];

	REPORT_BUF buf;
	memset(&buf, 0, sizeof(buf));
	buf.report.u.cmd        = CMD_WRITE;
	buf.report.u.rw.flash   = sector;
	buf.report.u.rw.address = swap16(offset);
	buf.report.u.rw.nBytes  = swap16(nBytes);

	if (InternalWrite(&buf) != (int)sizeof(REPORT_BUF)) {
		fprintf(stderr, "ERROR: Failed to send CMD_WRITE.\n");
		return false;
	}
	sleep_ms(2);  /* extra delay before streaming data */

	uint16 remaining = nBytes;
	uint16 reported  = remaining;
	while (remaining) {
		uint16 chunk = (remaining < (uint16)(CMD_SIZE - 1))
		               ? remaining : (uint16)(CMD_SIZE - 1);

		memset(&buf, 0, sizeof(buf));
		memcpy(buf.report.u.buffer + 1, buffer, chunk);

		if (InternalWrite(&buf) != (int)sizeof(REPORT_BUF)) {
			fprintf(stderr, "ERROR: Failed to send write data chunk.\n");
			return false;
		}
		buffer    += chunk;
		remaining -= chunk;

		if ((reported / 100) != (remaining / 100)) {
			printf("  Writing: %5u bytes remaining...\n", remaining);
			reported = remaining;
		}
	}

	/*
	 * Verify checksum.
	 * NOTE: Some X-BIT firmware revisions do not echo back the checksum,
	 * so a mismatch is logged as a warning rather than a hard error.
	 */
	if (GetStatus()) {
		if (statusBuf.report.u.status.checkSum != checksum) {
			printf("  WARNING: Checksum mismatch (got 0x%02X, expected 0x%02X).\n"
			       "           This may be a known firmware limitation — verify after write.\n",
			       statusBuf.report.u.status.checkSum, checksum);
		}
	}

	return true;
}

bool XbitFlasher::EraseBlock(int sector)
{
	REPORT_BUF buf;
	memset(&buf, 0, sizeof(buf));
	buf.report.u.erase.cmd     = CMD_ERASE;
	buf.report.u.erase.flash   = (uchar)sector;
	buf.report.u.erase.address = 0;  /* always 0 for XBIT */

	if (InternalWrite(&buf) != (int)sizeof(REPORT_BUF)) {
		fprintf(stderr, "ERROR: Failed to send CMD_ERASE (sector %d).\n", sector);
		return false;
	}
	return true;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Layout helpers
 * ═══════════════════════════════════════════════════════════════════════════*/

int XbitFlasher::GetSizeForBank(int layout, int bank) const
{
	if (layout < 1 || layout > BANK_LAYOUT_COUNT ||
	    bank   < 1 || bank   > BANKS_MAX)
		return 0;
	return BANK_LAYOUT[layout - 1][bank - 1] * 1024;
}

int XbitFlasher::GetBlockCountForBank(int layout, int bank) const
{
	int size = GetSizeForBank(layout, bank);
	if (size == 0)
		return 0;
	if (size % BLOCK_SIZE != 0) {
		fprintf(stderr, "ERROR: Bank size %d is not a multiple of block size %d.\n",
		        size, BLOCK_SIZE);
		return -1;
	}
	return size / BLOCK_SIZE;
}

int XbitFlasher::GetStartBlockForBank(int layout, int bank) const
{
	int offset = 0;
	for (int b = 1; b < bank; ++b)
		offset += GetSizeForBank(layout, b);
	if (offset % BLOCK_SIZE != 0) {
		fprintf(stderr, "ERROR: Bank start offset %d is not block-aligned.\n", offset);
		return -1;
	}
	return offset / BLOCK_SIZE;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * High-level flash operations
 * ═══════════════════════════════════════════════════════════════════════════*/

bool XbitFlasher::EraseBank(int bank)
{
	if (IsDeviceWriteProtected()) {
		fprintf(stderr, "ERROR: Modchip is write-protected.\n"
		                "       Replug the USB cable and try again.\n");
		return false;
	}

	int start_block = GetStartBlockForBank(memory_layout_id, bank);
	int block_count = GetBlockCountForBank(memory_layout_id, bank);
	if (start_block < 0 || block_count <= 0) {
		fprintf(stderr, "ERROR: Invalid bank %d for layout %d.\n",
		        bank, memory_layout_id);
		return false;
	}

	if (!GetBus()) {
		fprintf(stderr, "ERROR: Failed to acquire bus before erasing bank %d.\n", bank);
		return false;
	}

	printf("Erasing bank %d (%d block(s) starting at block %d)...\n",
	       bank, block_count, start_block);

	for (int b = start_block; b < start_block + block_count; ++b) {
		printf("  Erasing block %d...\n", b);
		if (!EraseBlock(b)) {
			ReleaseBus();
			return false;
		}
	}

	if (!ReleaseBus()) {
		fprintf(stderr, "WARNING: Failed to release bus after erasing bank %d.\n", bank);
	}
	return true;
}

bool XbitFlasher::Format(int layout)
{
	if (layout < 1 || layout > BANK_LAYOUT_COUNT) {
		fprintf(stderr, "ERROR: Invalid layout %d. Valid range: 1-%d.\n",
		        layout, BANK_LAYOUT_COUNT);
		return false;
	}

	if (IsDeviceWriteProtected()) {
		fprintf(stderr, "ERROR: Modchip is write-protected.\n"
		                "       Replug the USB cable and try again.\n");
		return false;
	}

	if (!GetBus()) {
		fprintf(stderr, "ERROR: Failed to acquire bus before formatting.\n");
		return false;
	}

	printf("Formatting: erasing all %d blocks...\n", TOTAL_BLOCKS);
	for (int i = 0; i < TOTAL_BLOCKS; ++i) {
		printf("  Erasing block %d / %d...\n", i + 1, TOTAL_BLOCKS);
		if (!EraseBlock(i)) {
			ReleaseBus();
			return false;
		}
	}

	if (!SetPage(layout)) {
		fprintf(stderr, "ERROR: Failed to set memory layout %d.\n", layout);
		ReleaseBus();
		return false;
	}
	memory_layout_id = layout;

	if (!ReleaseBus()) {
		fprintf(stderr, "WARNING: Failed to release bus after format.\n");
	}

	printf("Format complete. Layout set to %d.\n", layout);
	return true;
}

bool XbitFlasher::FlashBank(int bank, const uchar *input_data, int data_length)
{
	int bank_size   = GetSizeForBank(memory_layout_id, bank);
	int start_block = GetStartBlockForBank(memory_layout_id, bank);
	int block_count = GetBlockCountForBank(memory_layout_id, bank);

	if (bank_size == 0) {
		fprintf(stderr, "ERROR: Bank %d does not exist in layout %d.\n",
		        bank, memory_layout_id);
		return false;
	}
	if (data_length != bank_size) {
		fprintf(stderr, "ERROR: File size %d bytes does not match bank %d size %d bytes.\n",
		        data_length, bank, bank_size);
		return false;
	}
	if (start_block < 0 || block_count <= 0)
		return false;

	if (IsDeviceWriteProtected()) {
		fprintf(stderr, "ERROR: Modchip is write-protected.\n"
		                "       Replug the USB cable and try again.\n");
		return false;
	}

	/* Erase (EraseBank handles bus acquire/release internally) */
	printf("Step 1/2: Erasing bank %d...\n", bank);
	if (!EraseBank(bank))
		return false;

	/* Short settle time after bulk erase */
	sleep_ms(2000);

	/* Write */
	printf("Step 2/2: Writing bank %d...\n", bank);
	if (!GetBus()) {
		fprintf(stderr, "ERROR: Failed to acquire bus before writing bank %d.\n", bank);
		return false;
	}

	static const int SECTORS_PER_BLOCK = BLOCK_SIZE / 0x8000;  /* = 2 */
	static const int SECTOR_SIZE       = 0x8000;                /* 32 KB */
	static const int MAX_WRITE_RETRIES = 10;

	for (int blk = 0; blk < block_count; ++blk) {
		for (int sec = 0; sec < SECTORS_PER_BLOCK; ++sec) {
			int    file_offset = blk * BLOCK_SIZE + sec * SECTOR_SIZE;
			uchar  abs_sector  = (uchar)(start_block + blk);
			uint16 sec_offset  = (uint16)(sec * SECTOR_SIZE);

			printf("  Block %d, sector %d (file offset 0x%06X)...\n",
			       start_block + blk, sec, file_offset);

			bool ok = false;
			for (int attempt = 1; attempt <= MAX_WRITE_RETRIES; ++attempt) {
				if (WriteFlash(abs_sector, sec_offset,
				               &input_data[file_offset],
				               (uint16)SECTOR_SIZE)) {
					ok = true;
					if (attempt > 1)
						printf("    Write succeeded on attempt %d.\n", attempt);
					break;
				}
				fprintf(stderr, "    Write attempt %d/%d failed — retrying...\n",
				        attempt, MAX_WRITE_RETRIES);
				sleep_ms(100);
			}

			if (!ok) {
				fprintf(stderr, "ERROR: Sector write failed after %d attempts. Aborting.\n",
				        MAX_WRITE_RETRIES);
				ReleaseBus();
				return false;
			}
		}
	}

	if (!ReleaseBus()) {
		fprintf(stderr, "WARNING: Failed to release bus after writing bank %d.\n", bank);
	}

	printf("Flash bank %d complete.\n", bank);
	return true;
}

bool XbitFlasher::ReadBank(int bank, uchar *output_data, int *num_bytes_read)
{
	int bank_size   = GetSizeForBank(memory_layout_id, bank);
	int start_block = GetStartBlockForBank(memory_layout_id, bank);
	int block_count = GetBlockCountForBank(memory_layout_id, bank);

	if (bank_size == 0) {
		fprintf(stderr, "ERROR: Bank %d does not exist in layout %d.\n",
		        bank, memory_layout_id);
		return false;
	}
	if (start_block < 0 || block_count <= 0)
		return false;

	if (IsDeviceWriteProtected()) {
		fprintf(stderr, "ERROR: Modchip is write-protected.\n"
		                "       Replug the USB cable and try again.\n");
		return false;
	}

	if (!GetBus()) {
		fprintf(stderr, "ERROR: Failed to acquire bus before reading bank %d.\n", bank);
		return false;
	}

	static const int SECTORS_PER_BLOCK = BLOCK_SIZE / 0x8000;
	static const int SECTOR_SIZE       = 0x8000;

	*num_bytes_read = 0;
	for (int blk = 0; blk < block_count; ++blk) {
		printf("  Reading block %d...\n", start_block + blk);
		for (int sec = 0; sec < SECTORS_PER_BLOCK; ++sec) {
			int    file_offset = blk * BLOCK_SIZE + sec * SECTOR_SIZE;
			uchar  abs_sector  = (uchar)(start_block + blk);
			uint16 sec_offset  = (uint16)(sec * SECTOR_SIZE);

			if (!ReadFlash(abs_sector, sec_offset,
			               &output_data[file_offset],
			               (uint16)SECTOR_SIZE)) {
				fprintf(stderr, "ERROR: Read failed at block %d sector %d.\n",
				        start_block + blk, sec);
				ReleaseBus();
				return false;
			}
			*num_bytes_read += SECTOR_SIZE;
		}
	}

	if (!ReleaseBus()) {
		fprintf(stderr, "WARNING: Failed to release bus after reading bank %d.\n", bank);
	}
	return true;
}

bool XbitFlasher::VerifyBank(int bank, const uchar *input_data, int data_length)
{
	int bank_size = GetSizeForBank(memory_layout_id, bank);
	if (bank_size == 0) {
		fprintf(stderr, "ERROR: Bank %d does not exist in layout %d.\n",
		        bank, memory_layout_id);
		return false;
	}
	if (data_length != bank_size) {
		fprintf(stderr, "ERROR: File size %d bytes does not match bank %d size %d bytes.\n",
		        data_length, bank, bank_size);
		return false;
	}

	/* Allocate read buffer on the heap — bank can be up to 2 MB */
	std::vector<uchar> read_buf((size_t)bank_size);
	int bytes_read = 0;

	printf("Reading bank %d for verification...\n", bank);
	if (!ReadBank(bank, read_buf.data(), &bytes_read))
		return false;

	if (bytes_read != bank_size) {
		fprintf(stderr, "ERROR: Only read %d of %d bytes from bank %d.\n",
		        bytes_read, bank_size, bank);
		return false;
	}

	if (memcmp(read_buf.data(), input_data, (size_t)data_length) != 0) {
		fprintf(stderr, "ERROR: Verification FAILED — data mismatch in bank %d.\n", bank);
		return false;
	}

	printf("Verification PASSED. Bank %d matches file.\n", bank);
	return true;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Print helpers
 * ═══════════════════════════════════════════════════════════════════════════*/

void XbitFlasher::PrintMemoryBankLayout() const
{
	printf("Memory layout table (sizes in KiB):\n");
	printf("  %-10s", "Layout");
	for (int b = 1; b <= BANKS_MAX; ++b)
		printf("  Bank%-4d", b);
	printf("\n");
	for (int lay = 1; lay <= BANK_LAYOUT_COUNT; ++lay) {
		printf("  %-10d", lay);
		for (int b = 1; b <= BANKS_MAX; ++b) {
			int sz = GetSizeForBank(lay, b);
			if (sz)
				printf("  %-8d", sz / 1024);
			else
				printf("  %-8s", "—");
		}
		printf("\n");
	}
}

void XbitFlasher::PrintBankSelection() const
{
	printf("DIP switch positions (ON=1, OFF=0):\n");
	printf("  %-8s  SW1   SW2   SW3\n", "Bank");
	for (int i = 0; i < BANKS_MAX; ++i) {
		int mask = BIOS_SELECT_SWITCHES[i];
		printf("  %-8d  %-5s %-5s %-5s\n", i + 1,
		       (mask & 1) ? "ON"  : "OFF",
		       (mask & 2) ? "ON"  : "OFF",
		       (mask & 4) ? "ON"  : "OFF");
	}
}

void XbitFlasher::PrintUsage(const char *argv0) const
{
	printf("X-BIT Modchip Flasher\n\n");
	printf("Usage:\n");
	printf("  %s r <layout> <bank> <file>   Read bank to file\n",  argv0);
	printf("  %s w <layout> <bank> <file>   Write file to bank\n", argv0);
	printf("  %s v <layout> <bank> <file>   Verify bank against file\n", argv0);
	printf("  %s f <layout>                 Format chip with layout\n\n", argv0);
	printf("Examples:\n");
	printf("  %s f 5                         Format chip, layout 5 (2 x 1 MB banks)\n", argv0);
	printf("  %s w 5 1 evox.bin              Write evox.bin to bank 1\n", argv0);
	printf("  %s r 5 1 readback.bin          Read bank 1 to readback.bin\n", argv0);
	printf("  %s v 5 1 evox.bin              Verify bank 1 matches evox.bin\n\n", argv0);
	printf("Note: Format the chip before the first use or when changing layouts.\n\n");
	PrintMemoryBankLayout();
	printf("\n");
	PrintBankSelection();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * File I/O helpers
 * ═══════════════════════════════════════════════════════════════════════════*/

static bool load_file(const char *path, std::vector<uchar> &data)
{
	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "ERROR: Cannot open '%s' for reading.\n", path);
		return false;
	}

	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	rewind(f);

	if (sz <= 0) {
		fprintf(stderr, "ERROR: File '%s' is empty or unreadable.\n", path);
		fclose(f);
		return false;
	}
	if (sz > MAX_BIOS_SIZE) {
		fprintf(stderr, "ERROR: File '%s' is %ld bytes — exceeds 2 MB maximum.\n",
		        path, sz);
		fclose(f);
		return false;
	}
	if (sz % BLOCK_SIZE != 0) {
		fprintf(stderr, "ERROR: File size %ld is not a multiple of block size %d.\n",
		        sz, BLOCK_SIZE);
		fclose(f);
		return false;
	}

	data.resize((size_t)sz);
	if (fread(data.data(), 1, (size_t)sz, f) != (size_t)sz) {
		fprintf(stderr, "ERROR: Short read from '%s'.\n", path);
		fclose(f);
		return false;
	}

	fclose(f);
	printf("Loaded '%s' (%ld bytes).\n", path, sz);
	return true;
}

static bool save_file(const char *path, const uchar *data, int size)
{
	FILE *f = fopen(path, "wb");
	if (!f) {
		fprintf(stderr, "ERROR: Cannot open '%s' for writing.\n", path);
		return false;
	}

	if (fwrite(data, 1, (size_t)size, f) != (size_t)size) {
		fprintf(stderr, "ERROR: Short write to '%s'.\n", path);
		fclose(f);
		return false;
	}

	fclose(f);
	printf("Saved %d bytes to '%s'.\n", size, path);
	return true;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * main
 * ═══════════════════════════════════════════════════════════════════════════*/

int main(int argc, char *argv[])
{
	if (argc < 2) {
		XbitFlasher().PrintUsage(argv[0]);
		return 1;
	}

	char mode = argv[1][0];

	/* Validate argument counts early */
	bool needs_bank_file = (mode == 'r' || mode == 'w' || mode == 'v');
	if (mode == 'f' && argc < 3) {
		fprintf(stderr, "ERROR: Format mode requires a layout argument.\n\n");
		XbitFlasher().PrintUsage(argv[0]);
		return 1;
	}
	if (needs_bank_file && argc < 5) {
		fprintf(stderr, "ERROR: Modes r/w/v require: layout bank file.\n\n");
		XbitFlasher().PrintUsage(argv[0]);
		return 1;
	}

	/* Parse layout */
	char *endp  = nullptr;
	int   layout = (int)strtol(argv[2], &endp, 10);
	if (!*argv[2] || (endp && *endp) ||
	    layout < 1 || layout > BANK_LAYOUT_COUNT) {
		fprintf(stderr, "ERROR: Invalid layout '%s'. Valid range: 1–%d.\n",
		        argv[2], BANK_LAYOUT_COUNT);
		return 1;
	}

	/* Parse bank (optional) */
	int         bank     = 0;
	const char *filename = nullptr;
	if (needs_bank_file) {
		bank = (int)strtol(argv[3], &endp, 10);
		if (!*argv[3] || (endp && *endp) || bank < 1 || bank > BANKS_MAX) {
			fprintf(stderr, "ERROR: Invalid bank '%s'. Valid range: 1–%d.\n",
			        argv[3], BANKS_MAX);
			return 1;
		}
		filename = argv[4];
	}

	printf("Mode: %c  Layout: %d", mode, layout);
	if (needs_bank_file)
		printf("  Bank: %d  File: %s", bank, filename);
	printf("\n\n");

	/* Open device */
	XbitFlasher flasher;
	if (!flasher.OpenDevice()) {
		fprintf(stderr, "ERROR: Could not connect to X-BIT modchip.\n");
		return 2;
	}

	/* Layout consistency check for r/w/v */
	if (needs_bank_file && flasher.memory_layout_id != layout) {
		fprintf(stderr,
		        "ERROR: Layout mismatch — chip reports layout %d, you specified %d.\n"
		        "       Either specify the correct layout or format the chip first.\n"
		        "       If this is wrong: replug USB and try again.\n",
		        flasher.memory_layout_id, layout);
		return 3;
	}

	/* Load BIOS data for modes that need it */
	std::vector<uchar> bios_data;
	if (mode == 'w' || mode == 'v') {
		if (!load_file(filename, bios_data))
			return 4;
	}

	/* Execute operation */
	int ret = 0;
	switch (mode) {
		case 'r': {
			int bank_size = flasher.GetSizeForBank(layout, bank);
			if (bank_size <= 0) {
				fprintf(stderr, "ERROR: Bank %d has no size in layout %d.\n",
				        bank, layout);
				ret = 5;
				break;
			}
			std::vector<uchar> read_buf((size_t)bank_size);
			int bytes_read = 0;
			printf("Reading bank %d to '%s'...\n", bank, filename);
			if (!flasher.ReadBank(bank, read_buf.data(), &bytes_read)) {
				fprintf(stderr, "ERROR: Read failed.\n");
				ret = 5;
				break;
			}
			if (!save_file(filename, read_buf.data(), bytes_read))
				ret = 5;
			break;
		}
		case 'w':
			printf("Writing '%s' to bank %d...\n", filename, bank);
			if (!flasher.FlashBank(bank, bios_data.data(), (int)bios_data.size())) {
				fprintf(stderr, "ERROR: Write failed.\n");
				ret = 5;
			}
			break;

		case 'v':
			printf("Verifying bank %d against '%s'...\n", bank, filename);
			if (!flasher.VerifyBank(bank, bios_data.data(), (int)bios_data.size())) {
				/* VerifyBank already printed the error */
				ret = 5;
			}
			break;

		case 'f':
			printf("Formatting chip with layout %d...\n", layout);
			if (!flasher.Format(layout)) {
				fprintf(stderr, "ERROR: Format failed.\n");
				ret = 5;
			}
			break;

		default:
			fprintf(stderr, "ERROR: Unknown mode '%c'.\n", mode);
			flasher.PrintUsage(argv[0]);
			ret = 1;
			break;
	}

	/* CloseDevice is called by the destructor — no goto needed */
	return ret;
}
