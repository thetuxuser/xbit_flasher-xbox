/*
 * xbit.h — X-BIT modchip protocol definitions and XbitFlasher class
 *
 * The X-BIT is a DMS3-team modchip for the Original Xbox based on an
 * ST Microelectronics STR750 (8051 core) and exposed as a USB HID device.
 */
#ifndef XBIT_H
#define XBIT_H

#ifdef __cplusplus
extern "C" {
#endif
#include "hidapi/hidapi.h"
#ifdef __cplusplus
}
#endif

#include <stdint.h>

/* ── HID commands ───────────────────────────────────────────────────────────*/
#define CMD_RESET       0x01
#define CMD_ERASE       0x02
#define CMD_WRITE       0x03
#define CMD_READ        0x04
#define CMD_GET_STATUS  0x05
#define CMD_SET_REGS    0x06  /* Unused? */
#define CMD_SET_PAGE    0x07
#define CMD_SET_VM      0x08

/* ── Flash selectors ────────────────────────────────────────────────────────*/
#define PRIMARY_FLASH   0
#define SECONDARY_FLASH 1

/* ── Report sizes ───────────────────────────────────────────────────────────*/
#define OUTPUT_REPORT_SIZE  64
#define FEATURE_REPORT_SIZE OUTPUT_REPORT_SIZE
#define CMD_SIZE            OUTPUT_REPORT_SIZE

/* ── Status flags (VM register) ─────────────────────────────────────────────*/
#define STATUS_BUS_FREE      0x01
#define STATUS_BUS_ATTACHED  0x02
#define STATUS_WRITE_PROTECT 0x80

/* ── Flash geometry ─────────────────────────────────────────────────────────*/
#define TOTAL_BLOCKS    0x20        /* 32 blocks */
#define BLOCK_SIZE      0x10000     /* 64 KB per block */
#define MAX_BIOS_SIZE   (2 * 1024 * 1024)  /* 2 MB */

/* ── USB device identity ────────────────────────────────────────────────────*/
#define ST_VENDOR_ID    0x0483
#define ST_PRODUCT_ID   0x0000
#define DEVICE_MFG      L"ST Microelectronics"
#define DEVICE_PRODUCT  L"DK3200 Evaluation Board"

/* ── Bank layout table ──────────────────────────────────────────────────────*/
#define BANK_LAYOUT_COUNT 6
#define BANKS_MAX         6

/*
 * Bank sizes in KiB, indexed [layout-1][bank-1].
 * A zero entry means the bank does not exist for that layout.
 *
 * Layout  Bank1   Bank2   Bank3   Bank4   Bank5   Bank6
 *   1     512     512     256     256     256     256
 *   2    1024     256     256     256     256      —
 *   3    1024     512     256     256      —       —
 *   4    1024     512     512      —       —       —
 *   5    1024    1024      —       —       —       —
 *   6    2048      —       —       —       —       —
 */
static const int BANK_LAYOUT[BANK_LAYOUT_COUNT][BANKS_MAX] = {
	{512,  512,  256, 256, 256, 256},
	{1024, 256,  256, 256, 256,   0},
	{1024, 512,  256, 256,   0,   0},
	{1024, 512,  512,   0,   0,   0},
	{1024, 1024,   0,   0,   0,   0},
	{2048,    0,   0,   0,   0,   0},
};

/*
 * DIP switch bit patterns for each BIOS bank.
 * Bit 0 = switch 1, bit 1 = switch 2, bit 2 = switch 3.
 */
static const uint8_t BIOS_SELECT_SWITCHES[BANKS_MAX] = {
	0x00,  /* Bank 1 — OFF OFF OFF */
	0x01,  /* Bank 2 — ON  OFF OFF */
	0x02,  /* Bank 3 — OFF ON  OFF */
	0x03,  /* Bank 4 — ON  ON  OFF */
	0x04,  /* Bank 5 — OFF OFF ON  */
	0x05,  /* Bank 6 — ON  OFF ON  */
};

/* ── Type aliases ───────────────────────────────────────────────────────────*/
typedef uint8_t  uchar;
typedef uint8_t  uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;

/* ── HID report structures ──────────────────────────────────────────────────*/
#pragma pack(push, 1)

typedef struct {
	union {
		uchar cmd;

		struct {
			uchar  cmd;      /* CMD_ERASE */
			uchar  flash;    /* PRIMARY_FLASH or SECONDARY_FLASH */
			uint16 address;  /* Any address in any sector */
		} erase;

		struct {
			uchar  cmd;      /* CMD_READ or CMD_WRITE */
			uchar  flash;    /* Sector number (XBIT uses this field for sector) */
			uint16 address;  /* Byte offset within sector (big-endian) */
			uint16 nBytes;   /* Byte count (big-endian) */
		} rw;

		struct {
			uchar cmd;   /* CMD_SET_PAGE or CMD_SET_VM */
			uchar page;  /* Desired page register value */
			uchar vm;    /* Desired VM register value */
		} setRegs;

		struct {
			uchar cmd;         /* CMD_GET_STATUS */
			uchar currentCmd;  /* Command currently being processed */
			uchar page;        /* Page register (encodes memory layout) */
			uchar vm;          /* VM register (encodes bus/wp state) */
			uchar ret;         /* Return value from flash routine */
			uchar checkSum;    /* Checksum for write commands */
		} status;

		uchar buffer[CMD_SIZE];
	} u;
} MCU_CMD, *PMCU_CMD;

typedef struct {
	unsigned char reportID;  /* Must be 0; Windows HID driver prefix byte */
	MCU_CMD       report;
} REPORT_BUF, *PREPORT_BUF;

#pragma pack(pop)

/* ── XbitFlasher class ──────────────────────────────────────────────────────*/
class XbitFlasher
{
public:
	int memory_layout_id;

	XbitFlasher();
	~XbitFlasher();

	bool OpenDevice();
	bool CloseDevice();

	bool Format(int layout);
	bool EraseBank(int bank);
	bool FlashBank(int bank, const uchar *input_data, int data_length);
	bool ReadBank(int bank, uchar *output_data, int *num_bytes_read);
	bool VerifyBank(int bank, const uchar *input_data, int data_length);

	void PrintMemoryBankLayout() const;
	void PrintBankSelection() const;
	void PrintUsage(const char *argv0) const;

private:
	hid_device *handle;
	bool        device_initialized;
	REPORT_BUF  statusBuf;

	/* Device state queries */
	bool  IsDeviceInitialized() const;
	bool  IsValidStatus() const;
	uchar GetCurrentCommand() const;
	uchar GetMemoryLayout() const;
	uchar GetVMState() const;
	bool  IsDeviceReady() const;
	bool  IsDeviceBusFree() const;
	bool  IsDeviceBusAttached() const;
	bool  IsDeviceWriteProtected() const;

	/* Low-level HID I/O */
	int  InternalRead(PREPORT_BUF output);
	int  InternalWrite(PREPORT_BUF input);

	/* Protocol commands */
	bool GetStatus();
	bool Reset();
	bool SetVM(uchar vm);
	bool GetBus();
	bool ReleaseBus();
	bool SetPage(int layout_id);
	bool ReadFlash(uchar sector, uint16 offset, uchar *buffer, uint16 nBytes);
	bool WriteFlash(uchar sector, uint16 offset, const uchar *buffer, uint16 nBytes);
	bool EraseBlock(int sector);

	/* Layout helpers */
	int  GetStartBlockForBank(int layout, int bank) const;
	int  GetBlockCountForBank(int layout, int bank) const;
	int  GetSizeForBank(int layout, int bank) const;
};

#endif /* XBIT_H */
