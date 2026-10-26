#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include "raw_payload_cases.h"

#define A733_LINE_CAPACITY 1024U
#define A733_SESSION_NAME_CAPACITY 32U
#define A733_DUMP_CHUNK_BYTES 4096U
#define A733_DEFAULT_DUMP_LINES 200U
#define A733_MAX_DUMP_LINES 1000000U
#define A733_DUMP_LINE_DELAY_MS 25U
#define A733_MAX_PATH_ARGUMENT_CHARS 32767U
#define A733_LOG_FLUSH_INTERVAL 64ULL
#define A733_TRACE_BUFFER_SIZE_KB 256U
#define A733_TRACE_MINIMUM_BUFFERS 16U
#define A733_TRACE_MAXIMUM_BUFFERS 64U
#define A733_SUMMARY_RECENT_LINES 64U

#define A733_DIAGNOSTIC_WRITE_LBA_PHASE_BEGIN 150U
#define A733_DIAGNOSTIC_WRITE_LBA_PHASE_CLASS 151U
#define A733_DIAGNOSTIC_WRITE_LBA_PHASE_READBACK_OK 152U
#define A733_DIAGNOSTIC_WRITE_LBA_PHASE_BEFORE_SUCCESS 153U
#define A733_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_LOGFILE_BEGIN 154U
#define A733_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_LOGFILE_ABORTED 155U
#define A733_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_LOGFILE_SUCCESS 156U
#define A733_DIAGNOSTIC_WRITE_LBA_PHASE_REDIRECT_LOGFILE_BEGIN 157U
#define A733_DIAGNOSTIC_WRITE_LBA_PHASE_REDIRECT_LOGFILE_ABORTED 158U
#define A733_DIAGNOSTIC_WRITE_LBA_PHASE_REDIRECT_LOGFILE_BLOCK 159U
#define A733_DIAGNOSTIC_WRITE_LBA_PHASE_REDIRECT_LOGFILE_SUCCESS 160U
#define A733_DIAGNOSTIC_WRITE_LBA_PHASE_NON_LOGFILE_STALL_BEGIN 161U
#define A733_DIAGNOSTIC_WRITE_LBA_PHASE_NON_LOGFILE_STALL_END 162U
#define A733_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_NON_LOGFILE_BEGIN 163U
#define A733_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_NON_LOGFILE_SUCCESS 164U
#define A733_DIAGNOSTIC_REDIRECT_SCRATCH_WRITE_RESET 170U
#define A733_DIAGNOSTIC_REDIRECT_SCRATCH_WRITE_ISSUE 171U
#define A733_DIAGNOSTIC_REDIRECT_SCRATCH_WRITE_CMD_DONE 172U
#define A733_DIAGNOSTIC_REDIRECT_SCRATCH_WRITE_RINT_ERROR 173U
#define A733_DIAGNOSTIC_REDIRECT_SCRATCH_WRITE_PIO 174U
#define A733_DIAGNOSTIC_REDIRECT_SCRATCH_WRITE_DATA_OVER 175U
#define A733_DIAGNOSTIC_REDIRECT_SCRATCH_WRITE_IDLE 176U
#define A733_DIAGNOSTIC_REDIRECT_SCRATCH_WRITE_RESET_AFTER 177U
#define A733_DIAGNOSTIC_REDIRECT_SCRATCH_WRITE_POST_PIO_READY 178U
#define A733_DIAGNOSTIC_REDIRECT_SCRATCH_READ_RESET 180U
#define A733_DIAGNOSTIC_REDIRECT_SCRATCH_READ_ISSUE 181U
#define A733_DIAGNOSTIC_REDIRECT_SCRATCH_READ_CMD_DONE 182U
#define A733_DIAGNOSTIC_REDIRECT_SCRATCH_READ_RINT_ERROR 183U
#define A733_DIAGNOSTIC_REDIRECT_SCRATCH_READ_PIO 184U
#define A733_DIAGNOSTIC_REDIRECT_SCRATCH_READ_DATA_OVER 185U
#define A733_DIAGNOSTIC_REDIRECT_SCRATCH_READ_IDLE 186U
#define A733_DIAGNOSTIC_REDIRECT_SCRATCH_READ_RESET_AFTER 187U
#define A733_DIAGNOSTIC_REDIRECT_SCRATCH_VERIFY_MISMATCH 188U
#define A733_DIAGNOSTIC_REDIRECT_SCRATCH_VERIFY_EXPECTED 189U
#define A733_DIAGNOSTIC_REDIRECT_SCRATCH_VERIFY_ACTUAL 190U
#define A733_DIAGNOSTIC_REDIRECT_SCRATCH_VERIFY_SUCCESS 191U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_BLOCK_BEGIN 200U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_PIO_DONE 201U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_DATA_OVER 202U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_DEFERRED_RW_END 203U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_NEXT_CMD_ISSUE 204U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_NEXT_CMD_DONE 205U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_FINAL_QUIESCE 206U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK10_HOLD_BEGIN 207U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK10_HOLD_END 208U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK5_DATA_OVER_HOLD_BEGIN 209U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK5_DATA_OVER_HOLD_END 210U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK7_DATA_OVER_HOLD_BEGIN 211U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK7_DATA_OVER_HOLD_END 212U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK49_DATA_OVER_HOLD_BEGIN 213U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK49_DATA_OVER_HOLD_END 214U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK36_BEGIN_HOLD_BEGIN 215U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK36_BEGIN_HOLD_END 216U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK60_PIO_HOLD_BEGIN 217U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK60_PIO_HOLD_END 218U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK2_PIO_HOLD_BEGIN 219U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK2_PIO_HOLD_END 220U
#define A733_DIAGNOSTIC_PHASE_CMD17_ARG0_ISSUE_HOLD_BEGIN 221U
#define A733_DIAGNOSTIC_PHASE_CMD17_ARG0_ISSUE_HOLD_END 222U
#define A733_DIAGNOSTIC_PHASE_CMD17_ARG21_ISSUE_HOLD_BEGIN 223U
#define A733_DIAGNOSTIC_PHASE_CMD17_ARG21_ISSUE_HOLD_END 224U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK55_NEXT_CMD_HOLD_BEGIN 225U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK55_NEXT_CMD_HOLD_END 226U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK27_NEXT_CMD_HOLD_BEGIN 227U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK27_NEXT_CMD_HOLD_END 228U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK27_PIO_HOLD_BEGIN 229U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK27_PIO_HOLD_END 230U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK1_BEGIN_HOLD_BEGIN 231U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK1_BEGIN_HOLD_END 232U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK8_PIO_HOLD_BEGIN 233U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK8_PIO_HOLD_END 234U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK17_NEXT_CMD_HOLD_BEGIN 235U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK17_NEXT_CMD_HOLD_END 236U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_A733SETUP_BLOCK12_PIO_HOLD_BEGIN 237U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_A733SETUP_BLOCK12_PIO_HOLD_END 238U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK38_POST_DATAOVER_HOLD_BEGIN 239U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK38_POST_DATAOVER_HOLD_END 240U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK40_NEXT_CMD_DONE_HOLD_BEGIN 241U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK40_NEXT_CMD_DONE_HOLD_END 242U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_ARG2_BLOCK29_PIO_HOLD_BEGIN 243U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_ARG2_BLOCK29_PIO_HOLD_END 244U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK92_DATA_OVER_HOLD_BEGIN 245U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK92_DATA_OVER_HOLD_END 246U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK95_NEXT_CMD_HOLD_BEGIN 247U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK95_NEXT_CMD_HOLD_END 248U
#define A733_DIAGNOSTIC_PHASE_CMD17_ARG10000_ISSUE_HOLD_BEGIN 249U
#define A733_DIAGNOSTIC_PHASE_CMD17_ARG10000_ISSUE_HOLD_END 250U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK33_DATA_OVER_HOLD_BEGIN 251U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK33_DATA_OVER_HOLD_END 252U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK35_DATA_OVER_HOLD_BEGIN 253U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK35_DATA_OVER_HOLD_END 254U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK59_NEXT_CMD_HOLD_BEGIN 255U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK59_NEXT_CMD_HOLD_END 256U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK59_NEXT_CMD_BUGCHECK 257U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK59_NEXT_CMD_DONE_BUGCHECK 258U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK59_DATA_OVER_BUGCHECK 259U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK60_NEXT_CMD_BUGCHECK 260U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK60_NEXT_CMD_DONE_BUGCHECK 261U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK60_DATA_OVER_BUGCHECK 262U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK61_NEXT_CMD_BUGCHECK 263U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK61_NEXT_CMD_DONE_BUGCHECK 264U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK61_DATA_OVER_BUGCHECK 265U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK62_NEXT_CMD_BUGCHECK 266U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK62_NEXT_CMD_DONE_BUGCHECK 267U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK62_DATA_OVER_BUGCHECK 268U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK63_NEXT_CMD_BUGCHECK 269U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK63_NEXT_CMD_DONE_BUGCHECK 270U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK63_DATA_OVER_BUGCHECK 271U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK64_NEXT_CMD_BUGCHECK 272U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK64_NEXT_CMD_DONE_BUGCHECK 273U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK64_DATA_OVER_BUGCHECK 274U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK65_NEXT_CMD_BUGCHECK 275U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK65_NEXT_CMD_DONE_BUGCHECK 276U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK65_DATA_OVER_BUGCHECK 277U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK66_NEXT_CMD_BUGCHECK 278U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK66_NEXT_CMD_DONE_BUGCHECK 279U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK66_DATA_OVER_BUGCHECK 280U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK67_NEXT_CMD_BUGCHECK 281U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK67_NEXT_CMD_DONE_BUGCHECK 282U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK67_DATA_OVER_BUGCHECK 283U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK68_NEXT_CMD_BUGCHECK 284U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK68_NEXT_CMD_DONE_BUGCHECK 285U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK68_DATA_OVER_BUGCHECK 286U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK69_NEXT_CMD_BUGCHECK 287U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK69_NEXT_CMD_DONE_BUGCHECK 288U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK69_DATA_OVER_BUGCHECK 289U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK70_NEXT_CMD_BUGCHECK 290U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK70_NEXT_CMD_DONE_BUGCHECK 291U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK70_DATA_OVER_BUGCHECK 292U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK71_NEXT_CMD_BUGCHECK 293U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK71_NEXT_CMD_DONE_BUGCHECK 294U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK71_DATA_OVER_BUGCHECK 295U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK72_NEXT_CMD_BUGCHECK 296U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK72_NEXT_CMD_DONE_BUGCHECK 297U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK72_DATA_OVER_BUGCHECK 298U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK73_NEXT_CMD_BUGCHECK 299U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK73_NEXT_CMD_DONE_BUGCHECK 300U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK73_DATA_OVER_BUGCHECK 301U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK74_NEXT_CMD_BUGCHECK 302U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK74_NEXT_CMD_DONE_BUGCHECK 303U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK74_DATA_OVER_BUGCHECK 304U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_TARGET_NEXT_CMD_BUGCHECK 305U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_TARGET_NEXT_CMD_DONE_BUGCHECK 306U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_TARGET_DATA_OVER_BUGCHECK 307U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_FINAL_LOOP_EXIT_BUGCHECK 308U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_FINAL_RESET_IDLE_BUGCHECK 309U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_FINAL_QUIESCE_BUGCHECK 310U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_FINAL_RETURN_BUGCHECK 311U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_SYNTH_DATA_OVER_IDLE 312U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_DATA_OVER_TIMEOUT_STATE 313U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_DATA_OVER_RESET_PATH 314U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_PIO_TIMEOUT_STATE 315U
#define A733_DIAGNOSTIC_READ_PHASE_SYNTH_PROTECTIVE_MBR 316U
#define A733_DIAGNOSTIC_PHASE_VIRTUAL_SETUP_ARG 317U
#define A733_DIAGNOSTIC_PHASE_LBA0_SETUP_VBR_TRIGGER 318U
#define A733_DIAGNOSTIC_READ_PHASE_PATCH_SETUP_VBR_HIDDEN 319U
#define A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_VBR_LOW_ARG0 320U
#define A733_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_ISSUE 321U
#define A733_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_MAPPED 322U
#define A733_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_CMD_WRITTEN 323U
#define A733_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_PIO_DONE 324U
#define A733_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_RINT 325U
#define A733_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_FINAL 326U
#define A733_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_DATA 327U
#define A733_DIAGNOSTIC_READ_PHASE_CMD18_ARG0_DATA_OVER_AS_RESPONSE 328U
#define A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_CMD18_ARG0_BLOCK8 329U
#define A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_CMD18_ARG0_RESPONSE 330U
#define A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_CMD18_ARG0_START_DATA 331U
#define A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_CMD18_ARG0_SYNC_HANDOFF 332U
#define A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_CMD18_ARG0_FULL_DPC 333U
#define A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_CMD18_ARG0_FULL_DPC_HANDOFF 334U
#define A733_DIAGNOSTIC_READ_PHASE_CMD_START_PREWAIT 335U
#define A733_DIAGNOSTIC_READ_PHASE_HLE_RETRY_BEGIN 336U
#define A733_DIAGNOSTIC_READ_PHASE_HLE_RETRY_RESET 337U
#define A733_DIAGNOSTIC_READ_PHASE_HLE_RETRY_ISSUE 338U
#define A733_DIAGNOSTIC_READ_PHASE_HLE_RETRY_CMD_DONE 339U
#define A733_DIAGNOSTIC_READ_PHASE_HLE_RETRY_RINT_ERROR 340U
#define A733_DIAGNOSTIC_READ_PHASE_HLE_RETRY_PIO 341U
#define A733_DIAGNOSTIC_READ_PHASE_HLE_RETRY_DATA_OVER 342U
#define A733_DIAGNOSTIC_READ_PHASE_HLE_RETRY_IDLE 343U
#define A733_DIAGNOSTIC_READ_PHASE_HLE_RETRY_RESET_AFTER 344U
#define A733_DIAGNOSTIC_READ_PHASE_HLE_RETRY_CMD_DONE_LATE 345U
#define A733_DIAGNOSTIC_READ_PHASE_HLE_RETRY_DONE 346U
#define A733_DIAGNOSTIC_READ_PHASE_SMALL_READ_FORCE_COMPLETION_RINT 347U
#define A733_DIAGNOSTIC_READ_PHASE_SYNTH_ADVERTISED_TAIL 348U
#define A733_DIAGNOSTIC_READ_PHASE_SETUP_BACKUP_VBR_REMAP 349U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_MFT_RECORD_DATA 350U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_MFT_ARG_MAP 351U
#define A733_DIAGNOSTIC_READ_PHASE_PARTITION_RELATIVE_MODE 352U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LOGFILE_RESTART_DATA 353U
#define A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_BOOT_SECTOR2_LOW_ARG 354U
#define A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_BACKUP_VBR_LOW_ARG 355U
#define A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_BOOT16_LOW_ARG0_START 356U
#define A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_BOOT_SECTOR15_LOW_ARG 357U
#define A733_DIAGNOSTIC_READ_PHASE_MFT_RECORD3_VOLUME_FLAGS_PATCH 358U
#define A733_DIAGNOSTIC_READ_SPLIT_PHASE_LOGFILE_OVERLAY_DATA 359U
#define A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_FAT32_ROOT_DIR 373U
#define A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_FAT32_FAT 374U
#define A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_FAT32_SOURCES_DIR 375U
#define A733_DIAGNOSTIC_READ_PHASE_SETUP_OPEN_ARM_LIMIT_FAIL 376U
#define A733_DIAGNOSTIC_READ_PHASE_ROOT_OPEN_FAIL_ARM 377U

#define A733_RINT_CMD_DONE (1U << 2)
#define A733_RINT_DATA_OVER (1U << 3)
#define A733_RINT_RX_DATA_REQ (1U << 5)

#define A733_DIAGNOSTIC_LBA_CLASS_PRE_PARTITION 1U
#define A733_DIAGNOSTIC_LBA_CLASS_ESP 2U
#define A733_DIAGNOSTIC_LBA_CLASS_A733SETUP 3U
#define A733_DIAGNOSTIC_LBA_CLASS_TAIL 4U

#define A733_DIAGNOSTIC_ESP_START_LBA 0x00010000UL
#define A733_DIAGNOSTIC_A733SETUP_START_LBA 0x00810000UL
#define A733_DIAGNOSTIC_A733SETUP_END_LBA 0x0EE6A800UL

static const WCHAR A733_SESSION_NAME[] = L"A733SmhcTrace";
static const GUID A733_PROVIDER_GUID = {
    0xB13D8EB4UL,
    0x3F46,
    0x4BE7,
    {0x9A, 0x92, 0x58, 0xF9, 0x1D, 0x7C, 0x1E, 0x40}
};

/*
 * Provider {B13D8EB4-3F46-4BE7-9A92-58F91D7C1E40} payload contract.
 * Each payload is a packed, little-endian ULONG array with no padding:
 *   1 lifecycle [stage,status,a,b,c,d]
 *   2 bus       [type,value,status,a,b,c]
 *   3 issue     [phase,requestType,cmd,arg,blocks,blockSize,length,status]
 *   4 interrupt [rint,imask,pending,events,errors,required,status,active]
 *   5 dpc       [phase,events,errors,required,status,deferred,blocks,length]
 *   6 transfer  [phase,cmd,arg,blocks,blockSize,length,status,rint]
 *   7 recovery  [stage,status,rint,statusReg,gctrl,imask]
 *   8 card      [present,known,sector,rint,imask,reason]
 *   9 response  [cmd,responseType,raw0,raw1,raw2,raw3]
 *  10 mmio      [reg,before,requested,after,reason,persistence]
 *  11 completion[stage,rint,imask,enabled,required,completion]
 */

typedef struct _A733_EVENT_SCHEMA {
    USHORT Id;
    USHORT UlongCount;
    const CHAR *Name;
    const CHAR * const *FieldNames;
} A733_EVENT_SCHEMA;

typedef struct _A733_LINE {
    CHAR Data[A733_LINE_CAPACITY];
    DWORD Length;
    BOOL Truncated;
} A733_LINE;

typedef struct _A733_TRACE_PROPERTIES_BUFFER {
    EVENT_TRACE_PROPERTIES Properties;
    WCHAR LoggerName[A733_SESSION_NAME_CAPACITY];
} A733_TRACE_PROPERTIES_BUFFER;

typedef struct _A733_ARGUMENTS {
    PWSTR Storage;
    PWSTR *Values;
    DWORD Count;
} A733_ARGUMENTS;

typedef struct _A733_SUMMARY_ENTRY {
    const CHAR *Name;
    ULONGLONG Count;
    A733_LINE FirstLine;
    A733_LINE LastLine;
    BOOL HasFirst;
    BOOL HasLast;
} A733_SUMMARY_ENTRY;

static const CHAR * const A733_LIFECYCLE_FIELDS[] = {
    "stage", "status", "a", "b", "c", "d"
};
static const CHAR * const A733_BUS_FIELDS[] = {
    "type", "value", "status", "a", "b", "c"
};
static const CHAR * const A733_ISSUE_FIELDS[] = {
    "phase", "requestType", "cmd", "arg", "blocks", "blockSize",
    "length", "status"
};
static const CHAR * const A733_INTERRUPT_FIELDS[] = {
    "rint", "imask", "pending", "events", "errors", "required",
    "status", "active"
};
static const CHAR * const A733_DPC_FIELDS[] = {
    "phase", "events", "errors", "required", "status", "deferred",
    "blocks", "length"
};
static const CHAR * const A733_TRANSFER_FIELDS[] = {
    "phase", "cmd", "arg", "blocks", "blockSize", "length", "status",
    "rint"
};
static const CHAR * const A733_RECOVERY_FIELDS[] = {
    "stage", "status", "rint", "statusReg", "gctrl", "imask"
};
static const CHAR * const A733_CARD_FIELDS[] = {
    "present", "known", "sector", "rint", "imask", "reason"
};
static const CHAR * const A733_RESPONSE_FIELDS[] = {
    "cmd", "responseType", "raw0", "raw1", "raw2", "raw3"
};
static const CHAR * const A733_MMIO_FIELDS[] = {
    "reg", "before", "requested", "after", "reason", "persistence"
};
static const CHAR * const A733_COMPLETION_FIELDS[] = {
    "stage", "rint", "imask", "enabled", "required", "completion"
};

static const A733_EVENT_SCHEMA A733_EVENT_SCHEMAS[] = {
    {1, (USHORT)ARRAYSIZE(A733_LIFECYCLE_FIELDS),
        "lifecycle", A733_LIFECYCLE_FIELDS},
    {2, (USHORT)ARRAYSIZE(A733_BUS_FIELDS), "bus", A733_BUS_FIELDS},
    {3, (USHORT)ARRAYSIZE(A733_ISSUE_FIELDS), "issue", A733_ISSUE_FIELDS},
    {4, (USHORT)ARRAYSIZE(A733_INTERRUPT_FIELDS),
        "interrupt", A733_INTERRUPT_FIELDS},
    {5, (USHORT)ARRAYSIZE(A733_DPC_FIELDS), "dpc", A733_DPC_FIELDS},
    {6, (USHORT)ARRAYSIZE(A733_TRANSFER_FIELDS),
        "transfer", A733_TRANSFER_FIELDS},
    {7, (USHORT)ARRAYSIZE(A733_RECOVERY_FIELDS),
        "recovery", A733_RECOVERY_FIELDS},
    {8, (USHORT)ARRAYSIZE(A733_CARD_FIELDS), "card", A733_CARD_FIELDS},
    {9, (USHORT)ARRAYSIZE(A733_RESPONSE_FIELDS),
        "response", A733_RESPONSE_FIELDS},
    {10, (USHORT)ARRAYSIZE(A733_MMIO_FIELDS),
        "mmio", A733_MMIO_FIELDS},
    {11, (USHORT)ARRAYSIZE(A733_COMPLETION_FIELDS),
        "completion", A733_COMPLETION_FIELDS}
};

static const CHAR * const A733_SUMMARY_MARKERS[] = {
    "stageText=writeSplitBlockBegin",
    "stageText=writeSplitLbaClass",
    "stageText=writeSplitReadbackMatched",
    "stageText=writeSplitBeforeSdportSuccess",
    "stageText=fakeNtfsLogFileWriteBegin",
    "stageText=fakeNtfsLogFileWriteSuccess",
    "stageText=redirectNtfsLogFileWriteBegin",
    "stageText=redirectNtfsLogFileWriteScratchBlock",
    "stageText=redirectNtfsLogFileWriteSuccess",
    "stageText=nonLogA733SetupWriteStallBegin",
    "stageText=nonLogA733SetupWriteStallEnd",
    "stageText=redirectScratchWriteResetFifo",
    "stageText=redirectScratchWriteIssueCmd",
    "stageText=redirectScratchWriteStartReady",
    "stageText=redirectScratchWriteRintError",
    "stageText=redirectScratchWritePioWrite",
    "stageText=redirectScratchWritePostPioReady",
    "stageText=redirectScratchWriteDataOver",
    "stageText=redirectScratchWriteDataPathIdle",
    "stageText=redirectScratchWriteResetFifoAfter",
    "stageText=redirectScratchReadResetFifo",
    "stageText=redirectScratchReadIssueCmd",
    "stageText=redirectScratchReadCmdDone",
    "stageText=redirectScratchReadRintError",
    "stageText=redirectScratchReadPioRead",
    "stageText=redirectScratchReadDataOver",
    "stageText=redirectScratchReadDataPathIdle",
    "stageText=redirectScratchReadResetFifoAfter",
    "stageText=redirectScratchVerifyMismatch",
    "stageText=redirectScratchVerifyExpectedChecksum",
    "stageText=redirectScratchVerifyActualChecksum",
    "stageText=redirectScratchVerifySuccess",
    "stageText=readSplitBlockBegin",
    "stageText=readSplitPioDone",
    "stageText=readSplitDataOver",
    "stageText=readSplitDeferredRwEnd",
    "stageText=readSplitNextCmdIssue",
    "stageText=readSplitNextCmdDone",
    "stageText=readSplitFinalQuiesce",
    "stageText=readSplitLba0Block10HoldBegin",
    "stageText=readSplitLba0Block10HoldEnd",
    "stageText=readSplitLba0Block5DataOverHoldBegin",
    "stageText=readSplitLba0Block5DataOverHoldEnd",
    "stageText=readSplitLba0Block7DataOverHoldBegin",
    "stageText=readSplitLba0Block7DataOverHoldEnd",
    "stageText=readSplitLba0Block49DataOverHoldBegin",
    "stageText=readSplitLba0Block49DataOverHoldEnd",
    "stageText=readSplitLba0Block36BeginHoldBegin",
    "stageText=readSplitLba0Block36BeginHoldEnd",
    "stageText=readSplitLba0Block60PioHoldBegin",
    "stageText=readSplitLba0Block60PioHoldEnd",
    "stageText=readSplitLba0Block2PioHoldBegin",
    "stageText=readSplitLba0Block2PioHoldEnd",
    "stageText=cmd17Arg0IssueHoldBegin",
    "stageText=cmd17Arg0IssueHoldEnd",
    "stageText=cmd17Arg21IssueHoldBegin",
    "stageText=cmd17Arg21IssueHoldEnd",
    "stageText=cmd17Arg10000IssueHoldBegin",
    "stageText=cmd17Arg10000IssueHoldEnd",
    "stageText=readSplitLba0Block55NextCmdHoldBegin",
    "stageText=readSplitLba0Block55NextCmdHoldEnd",
    "stageText=readSplitLba0Block27NextCmdHoldBegin",
    "stageText=readSplitLba0Block27NextCmdHoldEnd",
    "stageText=readSplitLba0Block27PioHoldBegin",
    "stageText=readSplitLba0Block27PioHoldEnd",
    "stageText=readSplitLba0Block1BeginHoldBegin",
    "stageText=readSplitLba0Block1BeginHoldEnd",
    "stageText=readSplitLba0Block8PioHoldBegin",
    "stageText=readSplitLba0Block8PioHoldEnd",
    "stageText=readSplitLba0Block17NextCmdHoldBegin",
    "stageText=readSplitLba0Block17NextCmdHoldEnd",
    "stageText=readSplitA733SetupBlock12PioHoldBegin",
    "stageText=readSplitA733SetupBlock12PioHoldEnd",
    "stageText=readSplitLba0Block38PostDataOverHoldBegin",
    "stageText=readSplitLba0Block38PostDataOverHoldEnd",
    "stageText=readSplitLba0Block40NextCmdDoneHoldBegin",
    "stageText=readSplitLba0Block40NextCmdDoneHoldEnd",
    "stageText=readSplitArg2Block29PioHoldBegin",
    "stageText=readSplitArg2Block29PioHoldEnd",
    "stageText=readSplitLba0Block92DataOverHoldBegin",
    "stageText=readSplitLba0Block92DataOverHoldEnd",
    "stageText=readSplitLba0Block95NextCmdHoldBegin",
    "stageText=readSplitLba0Block95NextCmdHoldEnd",
    "stageText=readSplitLba0Block33DataOverHoldBegin",
    "stageText=readSplitLba0Block33DataOverHoldEnd",
    "stageText=readSplitLba0Block35DataOverHoldBegin",
    "stageText=readSplitLba0Block35DataOverHoldEnd",
    "stageText=readSplitLba0Block59NextCmdHoldBegin",
    "stageText=readSplitLba0Block59NextCmdHoldEnd",
    "stageText=readSplitLba0Block59NextCmdBugcheck",
    "stageText=readSplitLba0Block59NextCmdDoneBugcheck",
    "stageText=readSplitLba0Block59DataOverBugcheck",
    "stageText=readSplitLba0Block60NextCmdBugcheck",
    "stageText=readSplitLba0Block60NextCmdDoneBugcheck",
    "stageText=readSplitLba0Block60DataOverBugcheck",
    "stageText=readSplitLba0Block61NextCmdBugcheck",
    "stageText=readSplitLba0Block61NextCmdDoneBugcheck",
    "stageText=readSplitLba0Block61DataOverBugcheck",
    "stageText=readSplitLba0Block62NextCmdBugcheck",
    "stageText=readSplitLba0Block62NextCmdDoneBugcheck",
    "stageText=readSplitLba0Block62DataOverBugcheck",
    "stageText=readSplitLba0Block63NextCmdBugcheck",
    "stageText=readSplitLba0Block63NextCmdDoneBugcheck",
    "stageText=readSplitLba0Block63DataOverBugcheck",
    "stageText=readSplitLba0Block64NextCmdBugcheck",
    "stageText=readSplitLba0Block64NextCmdDoneBugcheck",
    "stageText=readSplitLba0Block64DataOverBugcheck",
    "stageText=readSplitLba0Block65NextCmdBugcheck",
    "stageText=readSplitLba0Block65NextCmdDoneBugcheck",
    "stageText=readSplitLba0Block65DataOverBugcheck",
    "stageText=readSplitLba0Block66NextCmdBugcheck",
    "stageText=readSplitLba0Block66NextCmdDoneBugcheck",
    "stageText=readSplitLba0Block66DataOverBugcheck",
    "stageText=readSplitLba0Block67NextCmdBugcheck",
    "stageText=readSplitLba0Block67NextCmdDoneBugcheck",
    "stageText=readSplitLba0Block67DataOverBugcheck",
    "stageText=readSplitLba0Block68NextCmdBugcheck",
    "stageText=readSplitLba0Block68NextCmdDoneBugcheck",
    "stageText=readSplitLba0Block68DataOverBugcheck",
    "stageText=readSplitLba0Block69NextCmdBugcheck",
    "stageText=readSplitLba0Block69NextCmdDoneBugcheck",
    "stageText=readSplitLba0Block69DataOverBugcheck",
    "stageText=readSplitLba0Block70NextCmdBugcheck",
    "stageText=readSplitLba0Block70NextCmdDoneBugcheck",
    "stageText=readSplitLba0Block70DataOverBugcheck",
    "stageText=readSplitLba0Block71NextCmdBugcheck",
    "stageText=readSplitLba0Block71NextCmdDoneBugcheck",
    "stageText=readSplitLba0Block71DataOverBugcheck",
    "stageText=readSplitLba0Block72NextCmdBugcheck",
    "stageText=readSplitLba0Block72NextCmdDoneBugcheck",
    "stageText=readSplitLba0Block72DataOverBugcheck",
    "stageText=readSplitLba0Block73NextCmdBugcheck",
    "stageText=readSplitLba0Block73NextCmdDoneBugcheck",
    "stageText=readSplitLba0Block73DataOverBugcheck",
    "stageText=readSplitLba0Block74NextCmdBugcheck",
    "stageText=readSplitLba0Block74NextCmdDoneBugcheck",
    "stageText=readSplitLba0Block74DataOverBugcheck",
    "stageText=readSplitTargetNextCmdBugcheck",
    "stageText=readSplitTargetNextCmdDoneBugcheck",
    "stageText=readSplitTargetDataOverBugcheck",
    "stageText=readSplitFinalLoopExitBugcheck",
    "stageText=readSplitFinalResetIdleBugcheck",
    "stageText=readSplitFinalQuiesceBugcheck",
    "stageText=readSplitFinalReturnBugcheck",
    "stageText=readSplitSynthDataOverIdle",
    "stageText=readSplitDataOverTimeoutState",
    "stageText=readSplitDataOverResetPath",
    "stageText=readSplitPioTimeoutState",
    "stageText=writeSuccessAlternateCompletion",
    "stageText=deferredCompleteEnter",
    "stageText=deferredCompleteExit",
    "stageText=deferredCompleteError",
    "stageText=writeSuccessRequestDpcPreComplete",
    "stageText=writeSuccessRequestDpcPostComplete",
    "stageText=writeCompletionTicketQueued",
    "stageText=writeCompletionTicketTimerDpc",
    "stageText=writeCompletionTicketRequestDpcPreComplete",
    "stageText=writeCompletionTicketRequestDpcPostComplete",
    "stageText=cmd7R1BTranResponse",
    "rawResponse=0x00000700",
    "normalizedResponse=0x00000900",
    "status=0xC0000103",
    "status=0xC00000B5",
    "requestStatus=0xC00000B5",
    "stageText=lastCommandForReset",
    "stageText=lastCommandDetail",
    "stageText=lastCommandDetailEvents",
    "stageText=smallReadRequest",
    "stageText=smallReadData",
    "stageText=smallReadPostCompletion",
    "stageText=smallReadCompletionState",
    "stageText=smallReadShapeAndRegs",
    "stageText=smallReadPostRegs",
    "stageText=smallReadRespRegsLow",
    "stageText=smallReadRespRegsHigh",
    "stageText=smallReadSynthTerminal",
    "stageText=smallReadSynthResetFifo",
    "stageText=smallReadSynthResetDataPath",
    "stageText=synthProtectiveMbrLba0",
    "stageText=virtualSetupArg",
    "stageText=patchSetupVbrHidden",
    "stageText=lowArgReadIssue",
    "stageText=lowArgReadMapped",
    "stageText=lowArgReadCmdWritten",
    "stageText=lowArgReadPioDone",
    "stageText=lowArgReadRint",
    "stageText=lowArgReadFinal",
    "stageText=lowArgReadData",
    "stageText=cmd18Arg0DataOverAsResponse",
    "stageText=synthSetupCmd18Arg0Block8",
    "stageText=synthSetupCmd18Arg0Response",
    "stageText=synthSetupCmd18Arg0StartData",
    "stageText=synthSetupCmd18Arg0SyncHandoff",
    "stageText=synthSetupCmd18Arg0FullDpc",
    "stageText=synthSetupCmd18Arg0FullDpcHandoff",
    "stageText=cmdStartPrewait",
    "stageText=readHleRetryBegin",
    "stageText=readHleRetryReset",
    "stageText=readHleRetryIssue",
    "stageText=readHleRetryCmdDone",
    "stageText=readHleRetryRintError",
    "stageText=readHleRetryPio",
    "stageText=readHleRetryDataOver",
    "stageText=readHleRetryIdle",
    "stageText=readHleRetryResetAfter",
    "stageText=readHleRetryCmdDoneLate",
    "stageText=readHleRetryDone",
    "stageText=smallReadForceCompletionRint",
    "stageText=synthAdvertisedTailRead",
    "stageText=setupBackupVbrRemap",
    "stageText=readSplitMftRecordData",
    "stageText=readSplitMftArgMap",
    "stageText=mftRecord3VolumeFlagsPatch",
    "stageText=partitionRelativeMode",
    "stageText=readSplitLogFileOverlayData",
    "stageText=readSplitLogFileRestartData",
    "stageText=synthSetupBootSector2LowArg",
    "stageText=synthSetupBackupVbrLowArg",
    "stageText=synthSetupBoot16LowArg0Start",
    "stageText=synthSetupBootSector15LowArg",
    "stageText=synthSetupFat32RootDir",
    "combo:setupVirtCmd17",
    "combo:setupVirtCmd18",
    "combo:setupVirtArg0",
    "combo:setupVirtArg10000",
    "combo:setupVirtCmd18Arg0",
    "combo:setupVirtCmd18Arg0Block8",
    "combo:setupVirtCmd18Arg10000",
    "combo:setupVirtCmd18Block8",
    "combo:setupVirtCmd18Block16",
    "combo:setupVirtHwSetupStart",
    "combo:cmd18Arg0Blk8IssueEnter",
    "combo:cmd18Arg0Blk8IssueExit",
    "combo:cmd18Arg0Blk8CommandDone",
    "combo:cmd18Arg0Blk8StartEntry",
    "combo:cmd18Arg0Blk8StartNoHandoff",
    "combo:cmd18Arg0Blk8ReadSplitBlock",
    "combo:cmd18Arg0Blk8ReadSplitFinal",
    "stageText=syntheticAcmd51Scr",
    "stageText=syntheticAcmd51StartTransfer",
    "stageText=cmd9R2Raw",
    "stageText=cmd9R2Out",
    "stageText=cmd2R2Raw",
    "stageText=cmd2R2Out",
    "stageText=cmd6R1ForceTran",
    "stageText=cmd6IgnoredCommandErrors",
    "stageText=cmd6IssuePlan",
    "stageText=cmd6IssueEvents",
    "stageText=cmd6IssueRegs",
    "stageText=cmd6IssueRespIrq",
    "stageText=cmd6IssueDataRegs",
    "stageText=cmd6SyntheticStatus",
    "stageText=cmd6DeferredQueueResult",
    "stageText=cmd6PreSdPortComplete",
    "stageText=cmd6PostSdPortComplete",
    "stageText=cmd6DpcIdentity",
    "stageText=cmd6StartTransferAlreadyComplete",
    "stageText=setClockAcceptedNoHwUpdate",
    "stageText=cmd9CsdCapacityPatch",
    "stageText=cmd9CsdWriteProtectClear",
    "stageText=syntheticAcmd51ScrWords",
    "stageText=acmdR1AppCmdClear",
    "stageText=commandDoneResponse",
    "stageText=commandDoneResponseMeta",
    "stageText=lastGetResponse",
    "stageText=issueRequestEnter",
    "stageText=issueRequestBusy",
    "stageText=issueRequestExit",
    "resetTypeName=SdResetTypeDat",
    "resetTypeName=SdResetTypeCmd",
    "resetTypeName=SdResetTypeAll",
    "combo:transferIoTimeout",
    "combo:issueIoTimeout",
    "combo:dpcIoTimeout",
    "combo:lifecycleIoTimeout",
    "combo:smallReadPostIoTimeout",
    "combo:lastCommandEventsIoTimeout",
    "combo:lastResetDat",
    "combo:lastResetCmd",
    "combo:lastResetAll",
    "combo:lastResetDatCmd7",
    "combo:issueCmd0",
    "combo:issueCmd2",
    "combo:issueCmd3",
    "combo:issueCmd6",
    "combo:issueCmd6NoTransfer",
    "combo:issueCmd6Transfer",
    "combo:transferCmd6",
    "combo:issueCmd7",
    "combo:issueCmd8",
    "combo:issueCmd9",
    "combo:issueCmd12",
    "combo:issueCmd13",
    "combo:issueCmd17",
    "combo:issueCmd18",
    "combo:issueCmd24",
    "combo:issueCmd25",
    "combo:issueCmd41",
    "combo:issueCmd42",
    "combo:issueCmd51",
    "combo:issueCmd55",
    "combo:issueCmd56",
    "combo:transferCmd17",
    "combo:cmd17TransferStart",
    "combo:cmd17TransferPioDone",
    "combo:cmd17TransferSuccessExit",
    "combo:cmd17TransferWaitPending",
    "combo:cmd17TransferWaitTimeout",
    "combo:cmd17TransferWaitDataOver",
    "combo:cmd17TransferIdle",
    "combo:cmd17TransferSynthTerminal",
    "combo:cmd17TransferArg0",
    "combo:cmd17TransferArgTail",
    "combo:cmd17TransferTailSynth",
    "combo:transferCmd18",
    "combo:transferCmd24",
    "combo:transferCmd25",
    "combo:transferCmd51",
    "combo:responseCmd2",
    "combo:responseCmd3",
    "combo:responseCmd6",
    "combo:responseCmd7",
    "combo:responseCmd8",
    "combo:responseCmd9",
    "combo:responseCmd13",
    "combo:responseCmd41",
    "combo:responseCmd42",
    "combo:responseCmd51",
    "combo:responseCmd55",
    "combo:smallReadReqCmd51",
    "combo:cmdDoneResp9",
    "combo:cmdDoneResp42",
    "combo:cmdDoneMetaR2",
    "combo:lastGetResp9",
    "combo:lastGetResp42",
    "combo:cmd9R2Raw",
    "combo:cmd9R2Out",
    "combo:cmd2R2Raw",
    "combo:cmd2R2Out",
    "combo:cmd6R1ForceTran",
    "combo:cmd6IgnoredCommandErrors",
    "combo:cmd6DeferredQueued",
    "combo:cmd6DeferredEnter",
    "combo:cmd6DeferredExit",
    "combo:cmd6DeferredError",
    "combo:cmd6SyntheticStatus",
    "combo:cmd6DeferredQueueResult",
    "combo:cmd6PreSdPortComplete",
    "combo:cmd6PostSdPortComplete",
    "combo:cmd6DpcIdentity",
    "combo:cmd6StartTransferAlreadyComplete",
    "combo:cmd9CsdCapacityPatch",
    "combo:cmd9CsdWriteProtectClear",
    "combo:readSplitSynthDataOverIdle",
    "combo:readSplitDataOverTimeoutState",
    "combo:readSplitDataOverResetPath",
    "combo:readSplitPioTimeoutState",
    "combo:lastGetResp6",
    "combo:syntheticScrWords",
    "combo:acmdR1AppCmdClear",
    "combo:busStatusSuccess",
    "combo:busStatusNotSupported",
    "combo:busStatusInvalidParameter",
    "combo:busStatusIoTimeout",
    "combo:busStatusPending",
    "busName=SdResetHw",
    "busName=SdResetHost",
    "busName=SdSetClock",
    "busName=SdSetVoltage",
    "busName=SdSetPower",
    "busName=SdSetBusWidth",
    "busName=SdSetBusSpeed",
    "busName=SdSetSignalingVoltage",
    "busValueName=SdResetTypeAll",
    "busValueName=SdResetTypeCmd",
    "busValueName=SdResetTypeDat",
    "busValueName=SdBusVoltage33",
    "busValueName=SdBusWidthUndefined",
    "busValueName=SdBusWidth1Bit",
    "busValueName=SdBusWidth4Bit",
    "busValueName=SdBusSpeedNormal",
    "busValueName=SdBusSpeedHigh",
    "busValueName=SdSignalingVoltage33",
    "busStatusName=STATUS_SUCCESS",
    "busStatusName=STATUS_NOT_SUPPORTED",
    "busStatusName=STATUS_INVALID_PARAMETER",
    "busStatusName=STATUS_IO_TIMEOUT",
    "busStatusName=STATUS_PENDING"
};

static HANDLE A733_LogHandle = INVALID_HANDLE_VALUE;
static DWORD A733_LogError = ERROR_SUCCESS;
static BOOL A733_LiveConsole = FALSE;
static BOOL A733_LiveInterestingOnly = FALSE;
static ULONGLONG A733_EventSequence = 0;
static ULONGLONG A733_LogLinesSinceFlush = 0;
static BYTE A733_DumpBuffer[A733_DUMP_CHUNK_BYTES];

__declspec(noinline)
static VOID
A733ZeroBytes(
    _Out_writes_bytes_all_(Length) PVOID Buffer,
    _In_ SIZE_T Length)
{
    volatile BYTE *Cursor;

    Cursor = (volatile BYTE *)Buffer;
    while (Length != 0) {
        *Cursor = 0;
        Cursor += 1;
        Length -= 1;
    }
}

__declspec(noinline)
static VOID
A733CopyBytes(
    _Out_writes_bytes_all_(Length) PVOID Destination,
    _In_reads_bytes_(Length) const VOID *Source,
    _In_ SIZE_T Length)
{
    volatile BYTE *WriteCursor;
    const volatile BYTE *ReadCursor;

    WriteCursor = (volatile BYTE *)Destination;
    ReadCursor = (const volatile BYTE *)Source;
    while (Length != 0) {
        *WriteCursor = *ReadCursor;
        WriteCursor += 1;
        ReadCursor += 1;
        Length -= 1;
    }
}

static VOID
A733LineAppendChar(
    _Inout_ A733_LINE *Line,
    _In_ CHAR Character)
{
    if (Line->Length < A733_LINE_CAPACITY) {
        Line->Data[Line->Length] = Character;
        Line->Length += 1;
    } else {
        Line->Truncated = TRUE;
    }
}

static VOID
A733LineAppendText(
    _Inout_ A733_LINE *Line,
    _In_z_ const CHAR *Text)
{
    while (*Text != '\0') {
        A733LineAppendChar(Line, *Text);
        Text += 1;
    }
}

static VOID
A733LineAppendDecimal(
    _Inout_ A733_LINE *Line,
    _In_ ULONGLONG Value)
{
    CHAR Digits[20];
    DWORD Count;

    Count = 0;
    do {
        Digits[Count] = (CHAR)('0' + (Value % 10ULL));
        Count += 1;
        Value /= 10ULL;
    } while ((Value != 0) && (Count < ARRAYSIZE(Digits)));

    while (Count != 0) {
        Count -= 1;
        A733LineAppendChar(Line, Digits[Count]);
    }
}

static VOID
A733LineAppendHex32(
    _Inout_ A733_LINE *Line,
    _In_ ULONG Value)
{
    static const CHAR HexDigits[] = "0123456789ABCDEF";
    ULONG Index;

    A733LineAppendText(Line, "0x");
    for (Index = 0; Index < 8; Index += 1) {
        A733LineAppendChar(
            Line,
            HexDigits[(Value >> (28U - (Index * 4U))) & 0xFUL]);
    }
}

static VOID
A733LineAppendHex64(
    _Inout_ A733_LINE *Line,
    _In_ ULONGLONG Value)
{
    static const CHAR HexDigits[] = "0123456789ABCDEF";
    ULONG Index;

    A733LineAppendText(Line, "0x");
    for (Index = 0; Index < 16; Index += 1) {
        A733LineAppendChar(
            Line,
            HexDigits[(ULONG)((Value >> (60U - (Index * 4U))) & 0xFULL)]);
    }
}

static BOOL
A733WriteAll(
    _In_ HANDLE File,
    _In_reads_bytes_(Length) const CHAR *Buffer,
    _In_ DWORD Length)
{
    DWORD Written;

    while (Length != 0) {
        Written = 0;
        if (!WriteFile(File, Buffer, Length, &Written, NULL)) {
            return FALSE;
        }
        if (Written == 0) {
            SetLastError(ERROR_WRITE_FAULT);
            return FALSE;
        }
        Buffer += Written;
        Length -= Written;
    }
    return TRUE;
}

static BOOL
A733WriteLineToHandle(
    _In_ HANDLE File,
    _Inout_ A733_LINE *Line,
    _In_ BOOL Flush)
{
    if (Line->Truncated || (Line->Length > (A733_LINE_CAPACITY - 2U))) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    A733LineAppendChar(Line, '\r');
    A733LineAppendChar(Line, '\n');
    if (!A733WriteAll(File, Line->Data, Line->Length)) {
        return FALSE;
    }
    if (Flush && !FlushFileBuffers(File)) {
        return FALSE;
    }
    return TRUE;
}

static BOOL
A733StringEquals(
    _In_z_ const CHAR *Left,
    _In_z_ const CHAR *Right)
{
    while ((*Left != '\0') && (*Right != '\0')) {
        if (*Left != *Right) {
            return FALSE;
        }
        Left += 1;
        Right += 1;
    }
    return (*Left == '\0') && (*Right == '\0');
}

static BOOL
A733LineContains(
    _In_ const A733_LINE *Line,
    _In_z_ const CHAR *Needle)
{
    DWORD Index;
    DWORD NeedleLength;
    DWORD Offset;

    NeedleLength = 0;
    while (Needle[NeedleLength] != '\0') {
        NeedleLength += 1;
    }
    if ((NeedleLength == 0) || (NeedleLength > Line->Length)) {
        return FALSE;
    }

    for (Offset = 0; Offset <= (Line->Length - NeedleLength); Offset += 1) {
        for (Index = 0; Index < NeedleLength; Index += 1) {
            if (Line->Data[Offset + Index] != Needle[Index]) {
                break;
            }
        }
        if (Index == NeedleLength) {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL
A733LineContainsEventName(
    _In_ const A733_LINE *Line,
    _In_z_ const CHAR *EventName)
{
    static const CHAR Prefix[] = " event=";
    DWORD EventLength;
    DWORD Index;
    DWORD Offset;
    DWORD PrefixLength;

    PrefixLength = ARRAYSIZE(Prefix) - 1U;
    EventLength = 0;
    while (EventName[EventLength] != '\0') {
        EventLength += 1;
    }
    if ((EventLength == 0) ||
        ((PrefixLength + EventLength + 1U) > Line->Length)) {
        return FALSE;
    }

    for (Offset = 0;
         Offset <= (Line->Length - PrefixLength - EventLength - 1U);
         Offset += 1) {
        for (Index = 0; Index < PrefixLength; Index += 1) {
            if (Line->Data[Offset + Index] != Prefix[Index]) {
                break;
            }
        }
        if (Index != PrefixLength) {
            continue;
        }
        for (Index = 0; Index < EventLength; Index += 1) {
            if (Line->Data[Offset + PrefixLength + Index] !=
                EventName[Index]) {
                break;
            }
        }
        if (Index != EventLength) {
            continue;
        }
        if (Line->Data[Offset + PrefixLength + EventLength] == ' ') {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL
A733TraceReadHexField(
    _In_ const A733_LINE *Line,
    _In_z_ const CHAR *Field,
    _Out_ DWORD *Value,
    _Out_ DWORD *FieldOffset)
{
    DWORD FieldLength = 0;
    DWORD Offset;
    BOOL Found = FALSE;

    while (Field[FieldLength] != '\0') {
        FieldLength += 1;
    }
#if A733_ACTUAL_NTFS_IMAGE_PROFILE
    /* Count the key even when a duplicate has a short or malformed value. */
    {
        DWORD KeyLength, Count = 0;
        if (Line->Truncated || FieldLength < 3U) return FALSE;
        KeyLength = FieldLength - 2U;
        if (KeyLength > Line->Length) return FALSE;
        for (Offset = 0; Offset <= Line->Length - KeyLength; Offset += 1) {
            DWORD Index;
            for (Index = 0; Index < KeyLength; Index += 1) {
                if (Line->Data[Offset + Index] != Field[Index]) break;
            }
            if (Index == KeyLength && ++Count > 1U) return FALSE;
        }
        if (Count != 1U) return FALSE;
    }
#endif
    if (Line->Truncated || FieldLength > Line->Length ||
        Line->Length - FieldLength < 8U) {
        return FALSE;
    }
    for (Offset = 0; Offset <= Line->Length - FieldLength - 8U; Offset += 1) {
        DWORD Index;
        DWORD Parsed = 0;
        for (Index = 0; Index < FieldLength; Index += 1) {
            if (Line->Data[Offset + Index] != Field[Index]) {
                break;
            }
        }
        if (Index != FieldLength) {
            continue;
        }
        if (Found) {
            return FALSE;
        }
        for (Index = 0; Index < 8U; Index += 1) {
            CHAR Digit = Line->Data[Offset + FieldLength + Index];
            if (Digit >= '0' && Digit <= '9') {
                Parsed = (Parsed << 4) | (DWORD)(Digit - '0');
            } else if (Digit >= 'A' && Digit <= 'F') {
                Parsed = (Parsed << 4) | (DWORD)(Digit - 'A' + 10);
            } else {
                return FALSE;
            }
        }
        Index = Offset + FieldLength + 8U;
        if (Index < Line->Length && Line->Data[Index] != ' ') {
            return FALSE;
        }
        *Value = Parsed;
        *FieldOffset = Offset;
        Found = TRUE;
    }
    return Found;
}

static BOOL
A733TraceIsValidActualMapping(
    _In_ const A733_LINE *Line)
{
    DWORD Cmd, Arg, Blocks, BlockSize, Length, Status, Hardware, HardwareText;
    DWORD Offsets[8];
    ULONGLONG End;
#if A733_ACTUAL_NTFS_IMAGE_PROFILE
    DWORD Phase, PhaseOffset;
    if (!A733TraceReadHexField(Line, " phase=0x", &Phase, &PhaseOffset) || Phase != 0x13DU) {
        return FALSE;
    }
#endif

    if (!A733LineContains(Line, " phase=0x0000013D cmd=0x") ||
        !A733TraceReadHexField(Line, " cmd=0x", &Cmd, &Offsets[0]) ||
        !A733TraceReadHexField(Line, " arg=0x", &Arg, &Offsets[1]) ||
        !A733TraceReadHexField(Line, " blocks=0x", &Blocks, &Offsets[2]) ||
        !A733TraceReadHexField(Line, " blockSize=0x", &BlockSize, &Offsets[3]) ||
        !A733TraceReadHexField(Line, " length=0x", &Length, &Offsets[4]) ||
        !A733TraceReadHexField(Line, " status=0x", &Status, &Offsets[5]) ||
        !A733TraceReadHexField(Line, " rint=0x", &Hardware, &Offsets[6]) ||
        !A733TraceReadHexField(Line, " hardwareArg=0x", &HardwareText, &Offsets[7])) {
        return FALSE;
    }
    // Only omit the canonical grammar accepted by the transcript verifier.
    if (Offsets[1] != Offsets[0] + sizeof(" cmd=0x") - 1U + 8U ||
        Offsets[2] != Offsets[1] + sizeof(" arg=0x") - 1U + 8U ||
        Offsets[3] != Offsets[2] + sizeof(" blocks=0x") - 1U + 8U ||
        Offsets[4] != Offsets[3] + sizeof(" blockSize=0x") - 1U + 8U ||
        Offsets[5] != Offsets[4] + sizeof(" length=0x") - 1U + 8U ||
        Offsets[6] != Offsets[5] + sizeof(" status=0x") - 1U + 8U) {
        return FALSE;
    }
    End = (ULONGLONG)Arg + Blocks;
    return Status == 0 && BlockSize == 512U && Blocks >= 1U &&
        Blocks <= A733_ACTUAL_FAT_MAX_BLOCKS && Length == Blocks * 512U &&
        ((Cmd == 17U && Blocks == 1U) || (Cmd == 18U && Blocks > 1U)) &&
#if A733_ACTUAL_NTFS_IMAGE_PROFILE
        Arg >= A733_ACTUAL_NTFS_START_LBA &&
        End <= A733_ACTUAL_NTFS_START_LBA + A733_ACTUAL_NTFS_SECTORS &&
        Hardware >= A733_ACTUAL_NTFS_PHYSICAL_LBA + (Arg - A733_ACTUAL_NTFS_START_LBA) &&
        (ULONGLONG)Hardware < A733_ACTUAL_NTFS_PHYSICAL_LBA + (End - A733_ACTUAL_NTFS_START_LBA) &&
        Hardware == HardwareText;
#else
        ((Arg >= A733_ACTUAL_FAT_START_LBA && End <= A733_ACTUAL_FAT_METADATA_END_LBA) ||
         (Arg >= A733_ACTUAL_FAT_FILE_START_LBA && End <= A733_ACTUAL_FAT_FILE_END_LBA)) &&
        Hardware >= Arg && (ULONGLONG)Hardware < End && Hardware == HardwareText;
#endif
}

static BOOL
A733ShouldMirrorActualFatLine(
    _In_ const A733_LINE *Line)
{
    if (A733LineContains(Line, "A733_SMHC_TRACE ") ||
        A733LineContains(Line, "payload=invalid") ||
        A733LineContains(Line, "loss ") ||
        A733LineContains(Line, "status=0xC") ||
        A733LineContains(Line, "status=0x8") ||
        A733LineContains(Line, " event=bus ") ||
        A733LineContains(Line, " event=recovery ") ||
        A733LineContains(Line, " event=card ") ||
        A733LineContains(Line, "stageText=moduleHint") ||
        A733LineContains(Line, "stageText=synth") ||
        A733LineContains(Line, "stageText=patchSetupVbrHidden") ||
        A733LineContains(Line, " stage=0x000003D2 ") ||
        A733LineContains(Line, " stage=0x000003D3 ") ||
        A733LineContains(Line, " stage=0x000003D4 ") ||
        A733LineContains(Line, " stage=0x000003D5 ") ||
        A733LineContains(Line, " phase=0x000003D0 ") ||
        A733LineContains(Line, " phase=0x00000175 ") ||
        A733LineContains(Line, " phase=0x00000176 ") ||
        A733LineContains(Line, " phase=0x00000177 ") ||
        A733LineContains(Line, " phase=0x00000138 ") ||
        A733LineContains(Line, " phase=0x0000013F ") ||
        A733LineContains(Line, " phase=0x00000041 ") ||
        A733LineContains(Line, " phase=0x00000043 ")) {
        return TRUE;
    }
    if (A733LineContains(Line, " cmd=0x00000006 ") ||
        A733LineContains(Line, " cmd=0x00000033 ")) {
        return TRUE;
    }
    if (A733LineContains(Line, " event=issue ") &&
        (A733LineContains(Line, " phase=0x00000001 ") ||
         A733LineContains(Line, " phase=0x00000002 "))) {
        return TRUE;
    }
    if (A733LineContains(Line, " event=transfer ") &&
        A733LineContains(Line, " phase=0x0000013D")) {
        // Preserve every mapping for the verified file request, including bad ones.
#if A733_ACTUAL_NTFS_IMAGE_PROFILE
        DWORD Arg, Blocks, FieldOffset;
        if (!A733TraceIsValidActualMapping(Line) ||
            !A733TraceReadHexField(Line, " arg=0x", &Arg, &FieldOffset) ||
            !A733TraceReadHexField(Line, " blocks=0x", &Blocks, &FieldOffset)) {
            return TRUE;
        }
        // Keep complete parents overlapping the pinned 160-sector file allocation.
        return (Arg < 0x34E0U && (ULONGLONG)Arg + Blocks > 0x3440U) ||
               A733LineContains(Line, " hardwareArg=0x00296008") ||
               A733LineContains(Line, " hardwareArg=0x002AB008") ||
               A733LineContains(Line, " hardwareArg=0x00296018");
#else
        return !A733TraceIsValidActualMapping(Line) ||
               A733LineContains(Line, " arg=0x00816020 blocks=0x00000008 ") ||
               A733LineContains(Line, " hardwareArg=0x00810000") ||
               A733LineContains(Line, " hardwareArg=0x0081202E") ||
               A733LineContains(Line, " hardwareArg=0x00814017") ||
               A733LineContains(Line, " hardwareArg=0x00816000");
#endif
    }
    return FALSE;
}

static BOOL
A733ShouldMirrorLine(
    _In_ const A733_LINE *Line)
{
    if (!A733_LiveConsole) {
        return FALSE;
    }
    if (A733_ACTUAL_FAT_VOLUME_PROFILE && A733_LiveInterestingOnly) {
        return A733ShouldMirrorActualFatLine(Line);
    }
    if (A733_LiveInterestingOnly) {
        return A733LineContains(Line, "A733_SMHC_TRACE ") ||
               A733LineContains(Line, "payload=invalid") ||
               A733LineContains(Line, "loss ") ||
               A733LineContains(Line, "stageText=cmd2R2") ||
               A733LineContains(Line, "stageText=cmd6") ||
               A733LineContains(Line, "CMD13_SEND_STATUS") ||
               A733LineContains(Line, "CMD17_READ_SINGLE_BLOCK") ||
               A733LineContains(Line, "CMD18_READ_MULTIPLE_BLOCK") ||
               A733LineContains(Line, "CMD24_WRITE_SINGLE_BLOCK") ||
               A733LineContains(Line, "CMD25_WRITE_MULTIPLE_BLOCK") ||
               A733LineContains(Line, "stageText=smallRead") ||
               A733LineContains(Line, "stageText=lastCommand") ||
               A733LineContains(Line, "stageText=commandDoneResponse") ||
               A733LineContains(Line, "stageText=lastGetResponse") ||
               A733LineContains(Line, "stageText=moduleHint") ||
               A733LineContains(Line, "stageText=redirect") ||
               A733LineContains(Line, "stageText=readSplit") ||
               A733LineContains(Line, "stageText=write") ||
               A733LineContains(Line, "stageText=deferredComplete") ||
               A733LineContains(Line, "event=recovery ") ||
               A733LineContains(Line, "event=card ") ||
               A733LineContains(Line, "status=0xC");
    }
    return A733LineContains(Line, "A733_SMHC_TRACE ") ||
           A733LineContains(Line, " event=lifecycle ") ||
           A733LineContains(Line, " event=issue ") ||
           A733LineContains(Line, " event=dpc ") ||
           A733LineContains(Line, " event=transfer ") ||
           A733LineContains(Line, " event=recovery ") ||
           A733LineContains(Line, " event=card ") ||
           A733LineContains(Line, " event=mmio ") ||
           A733LineContains(Line, " event=completion ");
}

static VOID
A733MirrorLineToConsole(
    _In_ const A733_LINE *Line)
{
    A733_LINE ConsoleLine;
    HANDLE Output;

    if (!A733ShouldMirrorLine(Line)) {
        return;
    }
    A733ZeroBytes(&ConsoleLine, sizeof(ConsoleLine));
    ConsoleLine.Length = Line->Length;
    ConsoleLine.Truncated = Line->Truncated;
    A733CopyBytes(ConsoleLine.Data, Line->Data, Line->Length);

    Output = GetStdHandle(STD_OUTPUT_HANDLE);
    if ((Output != NULL) && (Output != INVALID_HANDLE_VALUE)) {
        (VOID)A733WriteLineToHandle(Output, &ConsoleLine, FALSE);
    }
}

static VOID
A733CommitLogLine(
    _Inout_ A733_LINE *Line)
{
    A733MirrorLineToConsole(Line);
    if ((A733_LogHandle == NULL) ||
        (A733_LogHandle == INVALID_HANDLE_VALUE)) {
        return;
    }
    if (!A733WriteLineToHandle(A733_LogHandle, Line, FALSE)) {
        if (A733_LogError == ERROR_SUCCESS) {
            A733_LogError = GetLastError();
        }
        return;
    }
    A733_LogLinesSinceFlush += 1;
    if (A733_LogLinesSinceFlush >= A733_LOG_FLUSH_INTERVAL) {
        if (!FlushFileBuffers(A733_LogHandle)) {
            if (A733_LogError == ERROR_SUCCESS) {
                A733_LogError = GetLastError();
            }
            return;
        }
        A733_LogLinesSinceFlush = 0;
    }
}

static BOOL
A733FlushLog(VOID)
{
    if ((A733_LogHandle == NULL) ||
        (A733_LogHandle == INVALID_HANDLE_VALUE)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (!FlushFileBuffers(A733_LogHandle)) {
        if (A733_LogError == ERROR_SUCCESS) {
            A733_LogError = GetLastError();
        }
        return FALSE;
    }
    A733_LogLinesSinceFlush = 0;
    return TRUE;
}

static VOID
A733WriteConsoleText(
    _In_ DWORD StandardHandle,
    _In_z_ const CHAR *Text)
{
    HANDLE Output;
    DWORD Length;

    Length = 0;
    while (Text[Length] != '\0') {
        Length += 1;
    }
    Output = GetStdHandle(StandardHandle);
    if ((Output != NULL) && (Output != INVALID_HANDLE_VALUE)) {
        (VOID)A733WriteAll(Output, Text, Length);
    }
}

static VOID
A733ReportError(
    _In_z_ const CHAR *Operation,
    _In_ DWORD Error)
{
    A733_LINE Line;
    HANDLE Output;

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_TRACE_ERROR operation=");
    A733LineAppendText(&Line, Operation);
    A733LineAppendText(&Line, " win32=");
    A733LineAppendHex32(&Line, Error);
    Output = GetStdHandle(STD_ERROR_HANDLE);
    if ((Output != NULL) && (Output != INVALID_HANDLE_VALUE)) {
        (VOID)A733WriteLineToHandle(Output, &Line, FALSE);
    }
}

static ULONG
A733ReadPackedUlong(
    _In_reads_bytes_(sizeof(ULONG)) const BYTE *Data)
{
    return ((ULONG)Data[0]) |
           ((ULONG)Data[1] << 8U) |
           ((ULONG)Data[2] << 16U) |
           ((ULONG)Data[3] << 24U);
}

static const A733_EVENT_SCHEMA *
A733FindSchema(
    _In_ USHORT Id)
{
    DWORD Index;

    for (Index = 0; Index < ARRAYSIZE(A733_EVENT_SCHEMAS); Index += 1) {
        if (A733_EVENT_SCHEMAS[Index].Id == Id) {
            return &A733_EVENT_SCHEMAS[Index];
        }
    }
    return NULL;
}

static const CHAR *
A733BusOperationName(
    _In_ ULONG Type)
{
    switch (Type) {
    case 0U: return "SdBusOperationUndefined";
    case 1U: return "SdResetHw";
    case 2U: return "SdResetHost";
    case 3U: return "SdSetClock";
    case 4U: return "SdSetVoltage";
    case 5U: return "SdSetPower";
    case 6U: return "SdSetBusWidth";
    case 7U: return "SdSetBusSpeed";
    case 8U: return "SdSetSignalingVoltage";
    case 9U: return "SdSetDriveStrength";
    case 10U: return "SdSetDriverType";
    case 11U: return "SdSetPresetValue";
    case 12U: return "SdSetBlockGapInterrupt";
    case 13U: return "SdExecuteTuning";
    default: return "unknown";
    }
}

static const CHAR *
A733BusValueName(
    _In_ ULONG Type,
    _In_ ULONG Value)
{
    switch (Type) {
    case 2U:
        switch (Value) {
        case 0U: return "SdResetTypeUndefined";
        case 1U: return "SdResetTypeAll";
        case 2U: return "SdResetTypeCmd";
        case 3U: return "SdResetTypeDat";
        default: return "ResetTypeUnknown";
        }
    case 4U:
        switch (Value) {
        case 0U: return "SdBusVoltageUndefined";
        case 1U: return "SdBusVoltage33";
        case 2U: return "SdBusVoltage30";
        case 3U: return "SdBusVoltage18";
        case 4U: return "SdBusVoltageOff";
        default: return "VoltageUnknown";
        }
    case 6U:
        switch (Value) {
        case 0U: return "SdBusWidthUndefined";
        case 1U: return "SdBusWidth1Bit";
        case 4U: return "SdBusWidth4Bit";
        case 8U: return "SdBusWidth8Bit";
        default: return "BusWidthUnknown";
        }
    case 7U:
        switch (Value) {
        case 0U: return "SdBusSpeedUndefined";
        case 1U: return "SdBusSpeedNormal";
        case 2U: return "SdBusSpeedHigh";
        case 3U: return "SdBusSpeedSDR12";
        case 4U: return "SdBusSpeedSDR25";
        case 5U: return "SdBusSpeedSDR50";
        case 6U: return "SdBusSpeedDDR50";
        case 7U: return "SdBusSpeedSDR104";
        case 8U: return "SdBusSpeedHS200";
        case 9U: return "SdBusSpeedHS400";
        default: return "BusSpeedUnknown";
        }
    case 8U:
        switch (Value) {
        case 0U: return "SdSignalingVoltageUndefined";
        case 1U: return "SdSignalingVoltage33";
        case 2U: return "SdSignalingVoltage18";
        default: return "SignalingVoltageUnknown";
        }
    default:
        return "raw";
    }
}

static const CHAR *
A733NtStatusName(
    _In_ ULONG Status)
{
    switch (Status) {
    case 0x00000000UL: return "STATUS_SUCCESS";
    case 0x00000103UL: return "STATUS_PENDING";
    case 0xC000000DUL: return "STATUS_INVALID_PARAMETER";
    case 0xC00000B5UL: return "STATUS_IO_TIMEOUT";
    case 0xC00000BBUL: return "STATUS_NOT_SUPPORTED";
    default: return "STATUS_OTHER";
    }
}

static const CHAR *
A733SdCommandName(
    _In_ ULONG CommandIndex)
{
    switch (CommandIndex) {
    case 0U: return "CMD0_GO_IDLE_STATE";
    case 2U: return "CMD2_ALL_SEND_CID";
    case 3U: return "CMD3_SEND_RELATIVE_ADDR";
    case 6U: return "CMD6_SWITCH_OR_ACMD6_SET_BUS_WIDTH";
    case 7U: return "CMD7_SELECT_CARD";
    case 8U: return "CMD8_SEND_IF_COND";
    case 9U: return "CMD9_SEND_CSD";
    case 12U: return "CMD12_STOP_TRANSMISSION";
    case 13U: return "CMD13_SEND_STATUS";
    case 17U: return "CMD17_READ_SINGLE_BLOCK";
    case 18U: return "CMD18_READ_MULTIPLE_BLOCK";
    case 24U: return "CMD24_WRITE_SINGLE_BLOCK";
    case 25U: return "CMD25_WRITE_MULTIPLE_BLOCK";
    case 38U: return "CMD38_ERASE";
    case 41U: return "ACMD41_SD_SEND_OP_COND";
    case 42U: return "ACMD42_SET_CLR_CARD_DETECT";
    case 51U: return "ACMD51_SEND_SCR";
    case 55U: return "CMD55_APP_CMD";
    case 56U: return "CMD56_GEN_CMD";
    default: return "CMD_UNKNOWN";
    }
}

static const CHAR *
A733RequestTypeName(
    _In_ ULONG RequestType)
{
    switch (RequestType) {
    case 0U: return "SdRequestTypeUnspecified";
    case 1U: return "SdRequestTypeCommandNoTransfer";
    case 2U: return "SdRequestTypeCommandWithTransfer";
    case 3U: return "SdRequestTypeStartTransfer";
    default: return "SdRequestTypeUnknown";
    }
}

static const CHAR *
A733ModuleHintName(
    _In_ ULONG Tag)
{
    switch (Tag) {
    case 1U: return "a733smhc!A733SmhcIssueRequest";
    case 2U: return "a733smhc!SmhcStartTransfer";
    case 3U: return "sdport!SdPortCompleteRequest";
    case 4U: return "ntoskrnl!KeStallExecutionProcessor";
    default: return "unknown";
    }
}

static const CHAR *
A733CommandTypeName(
    _In_ ULONG CommandType)
{
    switch (CommandType) {
    case 0U: return "SdCommandTypeUndefined";
    case 1U: return "SdCommandTypeSuspend";
    case 2U: return "SdCommandTypeResume";
    case 3U: return "SdCommandTypeAbort";
    default: return "SdCommandTypeUnknown";
    }
}

static const CHAR *
A733CommandClassName(
    _In_ ULONG CommandClass)
{
    switch (CommandClass) {
    case 0U: return "SdCommandClassUndefined";
    case 1U: return "SdCommandClassStandard";
    case 2U: return "SdCommandClassApp";
    default: return "SdCommandClassUnknown";
    }
}

static const CHAR *
A733ResponseTypeName(
    _In_ ULONG ResponseType)
{
    switch (ResponseType) {
    case 1U: return "SdResponseTypeNone";
    case 2U: return "SdResponseTypeR1";
    case 3U: return "SdResponseTypeR1B";
    case 4U: return "SdResponseTypeR2";
    case 5U: return "SdResponseTypeR3";
    case 6U: return "SdResponseTypeR4";
    case 7U: return "SdResponseTypeR5";
    case 8U: return "SdResponseTypeR5B";
    case 9U: return "SdResponseTypeR6";
    default: return "SdResponseTypeUnknown";
    }
}

static const CHAR *
A733TransferTypeName(
    _In_ ULONG TransferType)
{
    switch (TransferType) {
    case 0U: return "SdTransferTypeUndefined";
    case 1U: return "SdTransferTypeNone";
    case 2U: return "SdTransferTypeSingleBlock";
    case 3U: return "SdTransferTypeMultiBlock";
    case 4U: return "SdTransferTypeMultiBlockNoStop";
    default: return "SdTransferTypeUnknown";
    }
}

static const CHAR *
A733TransferDirectionName(
    _In_ ULONG TransferDirection)
{
    switch (TransferDirection) {
    case 0U: return "SdTransferDirectionUndefined";
    case 1U: return "SdTransferDirectionRead";
    case 2U: return "SdTransferDirectionWrite";
    default: return "SdTransferDirectionUnknown";
    }
}

static const CHAR *
A733TransferMethodName(
    _In_ ULONG TransferMethod)
{
    switch (TransferMethod) {
    case 0U: return "SdTransferMethodUndefined";
    case 1U: return "SdTransferMethodPio";
    case 2U: return "SdTransferMethodSgDma";
    default: return "SdTransferMethodUnknown";
    }
}

static const CHAR *
A733ResetTypeName(
    _In_ ULONG ResetType)
{
    switch (ResetType) {
    case 0U: return "SdResetTypeUndefined";
    case 1U: return "SdResetTypeAll";
    case 2U: return "SdResetTypeCmd";
    case 3U: return "SdResetTypeDat";
    default: return "SdResetTypeUnknown";
    }
}

static const CHAR *
A733R1CurrentStateName(
    _In_ ULONG CurrentState)
{
    switch (CurrentState) {
    case 0U: return "IDLE";
    case 1U: return "READY";
    case 2U: return "IDENT";
    case 3U: return "STBY";
    case 4U: return "TRAN";
    case 5U: return "DATA";
    case 6U: return "RCV";
    case 7U: return "PRG";
    case 8U: return "DIS";
    default: return "UNKNOWN";
    }
}

static BOOL
A733IsR1LikeResponse(
    _In_ ULONG ResponseType)
{
    return (BOOL)((ResponseType == 2U) || (ResponseType == 3U));
}

static VOID
A733AppendR1Decode(
    _Inout_ A733_LINE *Line,
    _In_ ULONG RawResponse)
{
    ULONG CurrentState;

    CurrentState = (RawResponse >> 9U) & 0xFU;
    A733LineAppendText(Line, " r1CurrentState=");
    A733LineAppendDecimal(Line, CurrentState);
    A733LineAppendChar(Line, '(');
    A733LineAppendText(Line, A733R1CurrentStateName(CurrentState));
    A733LineAppendChar(Line, ')');
    A733LineAppendText(Line, " r1ReadyForData=");
    A733LineAppendDecimal(Line, (RawResponse >> 8U) & 1U);
    A733LineAppendText(Line, " r1AppCmd=");
    A733LineAppendDecimal(Line, (RawResponse >> 5U) & 1U);
    A733LineAppendText(Line, " r1OtherStatusMask=");
    A733LineAppendHex32(Line, RawResponse & 0xFDFFE008UL);
}

static VOID
A733AppendResponseDecode(
    _Inout_ A733_LINE *Line,
    _In_ ULONG CommandIndex,
    _In_ ULONG ResponseType,
    _In_ ULONG RawResponse)
{
    A733LineAppendText(Line, " cmdName=");
    A733LineAppendText(Line, A733SdCommandName(CommandIndex));
    A733LineAppendText(Line, " cmdIndexDec=");
    A733LineAppendDecimal(Line, CommandIndex);
    A733LineAppendText(Line, " cmdIndexHex=");
    A733LineAppendHex32(Line, CommandIndex);
    A733LineAppendText(Line, " responseName=");
    A733LineAppendText(Line, A733ResponseTypeName(ResponseType));
    if (A733IsR1LikeResponse(ResponseType)) {
        A733AppendR1Decode(Line, RawResponse);
    }
}

static VOID
A733AppendEventMaskDecode(
    _Inout_ A733_LINE *Line,
    _In_ ULONG Events)
{
    A733LineAppendText(Line, " eventsText=");
    if (Events == 0) {
        A733LineAppendText(Line, "none");
        return;
    }
    if ((Events & 0x00000001UL) != 0) {
        A733LineAppendText(Line, "CARD_RESPONSE,");
    }
    if ((Events & 0x00000002UL) != 0) {
        A733LineAppendText(Line, "CARD_RW_END,");
    }
    if ((Events & 0x00000008UL) != 0) {
        A733LineAppendText(Line, "DMA_COMPLETE,");
    }
    if ((Events & 0x00000010UL) != 0) {
        A733LineAppendText(Line, "BUFFER_EMPTY,");
    }
    if ((Events & 0x00000020UL) != 0) {
        A733LineAppendText(Line, "BUFFER_FULL,");
    }
    if ((Events & 0x000000C0UL) != 0) {
        A733LineAppendText(Line, "CARD_CHANGE,");
    }
    if ((Events & 0x00008000UL) != 0) {
        A733LineAppendText(Line, "ERROR,");
    }
}

static VOID
A733AppendCommandShapeDecode(
    _Inout_ A733_LINE *Line,
    _In_ ULONG Shape)
{
    ULONG CommandType;
    ULONG CommandClass;
    ULONG ResponseType;
    ULONG TransferType;

    CommandType = Shape & 0xFFU;
    CommandClass = (Shape >> 8U) & 0xFFU;
    ResponseType = (Shape >> 16U) & 0xFFU;
    TransferType = (Shape >> 24U) & 0xFFU;

    A733LineAppendText(Line, " commandTypeName=");
    A733LineAppendText(Line, A733CommandTypeName(CommandType));
    A733LineAppendText(Line, " commandClassName=");
    A733LineAppendText(Line, A733CommandClassName(CommandClass));
    A733LineAppendText(Line, " responseName=");
    A733LineAppendText(Line, A733ResponseTypeName(ResponseType));
    A733LineAppendText(Line, " transferTypeName=");
    A733LineAppendText(Line, A733TransferTypeName(TransferType));
}

static VOID
A733AppendTransferShapeDecode(
    _Inout_ A733_LINE *Line,
    _In_ ULONG Shape)
{
    ULONG TransferDirection;
    ULONG TransferMethod;
    ULONG AutoFlags;

    TransferDirection = Shape & 0xFFU;
    TransferMethod = (Shape >> 8U) & 0xFFU;
    AutoFlags = (Shape >> 16U) & 0xFFU;

    A733LineAppendText(Line, " transferDirectionName=");
    A733LineAppendText(Line, A733TransferDirectionName(TransferDirection));
    A733LineAppendText(Line, " transferMethodName=");
    A733LineAppendText(Line, A733TransferMethodName(TransferMethod));
    A733LineAppendText(Line, " useAutoCmd12=");
    A733LineAppendDecimal(Line, AutoFlags & 1U);
    A733LineAppendText(Line, " useAutoCmd23=");
    A733LineAppendDecimal(Line, (AutoFlags >> 1U) & 1U);
}

static ULONG
A733ClassifyDiagnosticWriteLba(
    _In_ ULONG Lba)
{
    if (Lba < A733_DIAGNOSTIC_ESP_START_LBA) {
        return A733_DIAGNOSTIC_LBA_CLASS_PRE_PARTITION;
    }
    if (Lba < A733_DIAGNOSTIC_A733SETUP_START_LBA) {
        return A733_DIAGNOSTIC_LBA_CLASS_ESP;
    }
    if (Lba < A733_DIAGNOSTIC_A733SETUP_END_LBA) {
        return A733_DIAGNOSTIC_LBA_CLASS_A733SETUP;
    }
    return A733_DIAGNOSTIC_LBA_CLASS_TAIL;
}

static const CHAR *
A733DiagnosticWriteLbaClassName(
    _In_ ULONG Class)
{
    switch (Class) {
    case A733_DIAGNOSTIC_LBA_CLASS_PRE_PARTITION:
        return "pre-partition";
    case A733_DIAGNOSTIC_LBA_CLASS_ESP:
        return "ESP";
    case A733_DIAGNOSTIC_LBA_CLASS_A733SETUP:
        return "A733SETUP";
    case A733_DIAGNOSTIC_LBA_CLASS_TAIL:
        return "tail-or-backup-GPT";
    default:
        return "unknown";
    }
}

static VOID
A733AppendDiagnosticWriteLbaInfo(
    _Inout_ A733_LINE *Line,
    _In_ ULONG Lba)
{
    ULONG Class;

    Class = A733ClassifyDiagnosticWriteLba(Lba);
    A733LineAppendText(Line, " currentLba=");
    A733LineAppendHex32(Line, Lba);
    A733LineAppendText(Line, " currentLbaDec=");
    A733LineAppendDecimal(Line, Lba);
    A733LineAppendText(Line, " lbaClass=");
    A733LineAppendHex32(Line, Class);
    A733LineAppendText(Line, " lbaClassName=");
    A733LineAppendText(Line, A733DiagnosticWriteLbaClassName(Class));
}

static VOID
A733AppendReadSplitProgressInfo(
    _Inout_ A733_LINE *Line,
    _In_reads_bytes_(8U * sizeof(ULONG)) const BYTE *Payload,
    _In_ ULONG Packed)
{
    ULONG ParentArgument;
    ULONG ParentBlockCount;
    ULONG BlockIndex;
    ULONG BlocksRemaining;
    ULONG CurrentLba;

    ParentArgument = A733ReadPackedUlong(Payload + (2U * sizeof(ULONG)));
    ParentBlockCount = A733ReadPackedUlong(Payload + (3U * sizeof(ULONG)));
    BlockIndex = Packed & 0xFFFFU;
    BlocksRemaining = (Packed >> 16U) & 0xFFFFU;

    A733LineAppendText(Line, " blockIndex=");
    A733LineAppendDecimal(Line, BlockIndex);
    A733LineAppendText(Line, " blocksRemaining=");
    A733LineAppendDecimal(Line, BlocksRemaining);
    A733LineAppendText(Line, " parentBlockCount=");
    A733LineAppendDecimal(Line, ParentBlockCount);

    CurrentLba = ParentArgument + BlockIndex;
    if (CurrentLba >= ParentArgument) {
        A733LineAppendText(
            Line,
            " currentLbaSource=parentArgPlusBlockIndexIfSector");
        A733AppendDiagnosticWriteLbaInfo(Line, CurrentLba);
    } else {
        A733LineAppendText(Line, " currentLbaOverflow=yes");
    }
}

static VOID
A733AppendIssueDecode(
    _Inout_ A733_LINE *Line,
    _In_reads_bytes_(8U * sizeof(ULONG)) const BYTE *Payload)
{
    ULONG CommandIndex;

    CommandIndex = A733ReadPackedUlong(Payload + (2U * sizeof(ULONG)));
    A733LineAppendText(Line, " cmdName=");
    A733LineAppendText(Line, A733SdCommandName(CommandIndex));
    A733LineAppendText(Line, " cmdIndexDec=");
    A733LineAppendDecimal(Line, CommandIndex);
}

static VOID
A733AppendLowArgReadRintInfo(
    _Inout_ A733_LINE *Line,
    _In_z_ const CHAR *Name,
    _In_ ULONG Rint)
{
    A733LineAppendChar(Line, ' ');
    A733LineAppendText(Line, Name);
    A733LineAppendChar(Line, '=');
    A733LineAppendHex32(Line, Rint);
    A733LineAppendText(Line, " cmdDone=");
    A733LineAppendDecimal(Line, ((Rint & A733_RINT_CMD_DONE) != 0) ? 1U : 0U);
    A733LineAppendText(Line, " dataOver=");
    A733LineAppendDecimal(Line, ((Rint & A733_RINT_DATA_OVER) != 0) ? 1U : 0U);
    A733LineAppendText(Line, " rxDataReq=");
    A733LineAppendDecimal(Line, ((Rint & A733_RINT_RX_DATA_REQ) != 0) ? 1U : 0U);
}

static VOID
A733AppendTransferDecode(
    _Inout_ A733_LINE *Line,
    _In_reads_bytes_(8U * sizeof(ULONG)) const BYTE *Payload)
{
    ULONG Stage;
    ULONG CommandIndex;
    ULONG Packed;
    ULONG LbaClass;
    ULONG BlockIndex;
    ULONG BlocksRemaining;
    ULONG OriginalBlockCount;

    Stage = A733ReadPackedUlong(Payload);
    CommandIndex = A733ReadPackedUlong(Payload + sizeof(ULONG));
    Packed = A733ReadPackedUlong(Payload + (7U * sizeof(ULONG)));
    A733LineAppendText(Line, " cmdName=");
    A733LineAppendText(Line, A733SdCommandName(CommandIndex));
    A733LineAppendText(Line, " cmdIndexDec=");
    A733LineAppendDecimal(Line, CommandIndex);

    switch (Stage) {
    case A733_DIAGNOSTIC_WRITE_LBA_PHASE_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=writeSplitBlockBegin"
            " currentLbaSource=transfer.rint");
        A733AppendDiagnosticWriteLbaInfo(Line, Packed);
        break;
    case A733_DIAGNOSTIC_WRITE_LBA_PHASE_CLASS:
        LbaClass = Packed & 0xFFU;
        BlockIndex = (Packed >> 8U) & 0xFFU;
        BlocksRemaining = (Packed >> 16U) & 0xFFU;
        OriginalBlockCount = (Packed >> 24U) & 0xFFU;
        A733LineAppendText(Line, " stageText=writeSplitLbaClass");
        A733LineAppendText(Line, " lbaClass=");
        A733LineAppendHex32(Line, LbaClass);
        A733LineAppendText(Line, " lbaClassName=");
        A733LineAppendText(Line, A733DiagnosticWriteLbaClassName(LbaClass));
        A733LineAppendText(Line, " blockIndex=");
        A733LineAppendDecimal(Line, BlockIndex);
        A733LineAppendText(Line, " blocksRemaining=");
        A733LineAppendDecimal(Line, BlocksRemaining);
        A733LineAppendText(Line, " originalBlockCountLow8=");
        A733LineAppendDecimal(Line, OriginalBlockCount);
        break;
    case A733_DIAGNOSTIC_WRITE_LBA_PHASE_READBACK_OK:
        A733LineAppendText(
            Line,
            " stageText=writeSplitReadbackMatched"
            " currentLbaSource=transfer.rint");
        A733AppendDiagnosticWriteLbaInfo(Line, Packed);
        break;
    case A733_DIAGNOSTIC_WRITE_LBA_PHASE_BEFORE_SUCCESS:
        A733LineAppendText(Line, " stageText=writeSplitBeforeSdportSuccess");
        A733LineAppendText(Line, " originalBlockCount=");
        A733LineAppendDecimal(Line, Packed);
        break;
    case 65U:
        A733LineAppendText(Line, " stageText=smallReadSynthTerminal statusReg=");
        A733LineAppendHex32(Line, Packed);
        break;
    case 66U:
        A733LineAppendText(Line, " stageText=smallReadSynthResetFifo statusReg=");
        A733LineAppendHex32(Line, Packed);
        break;
    case 67U:
        A733LineAppendText(Line, " stageText=smallReadSynthResetDataPath statusReg=");
        A733LineAppendHex32(Line, Packed);
        break;
    case 68U:
        A733LineAppendText(Line, " stageText=syntheticAcmd51Scr data0BytesLE=");
        A733LineAppendHex32(Line, Packed);
        break;
    case 69U:
        A733LineAppendText(Line, " stageText=syntheticAcmd51StartTransfer data0BytesLE=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_SYNTH_PROTECTIVE_MBR:
        A733LineAppendText(Line, " stageText=synthProtectiveMbrLba0");
        break;
    case A733_DIAGNOSTIC_PHASE_VIRTUAL_SETUP_ARG:
        A733LineAppendText(Line, " stageText=virtualSetupArg hardwareArg=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_PHASE_LBA0_SETUP_VBR_TRIGGER:
        A733LineAppendText(Line, " stageText=lba0SetupVbrModeTrigger arg=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_PATCH_SETUP_VBR_HIDDEN:
        A733LineAppendText(Line, " stageText=patchSetupVbrHidden sector=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_VBR_LOW_ARG0:
        A733LineAppendText(Line, " stageText=synthSetupVbrLowArg0 hardwareArg=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_ISSUE:
        A733LineAppendText(Line, " stageText=lowArgReadIssue");
        A733AppendLowArgReadRintInfo(Line, "rintBefore", Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_MAPPED:
        A733LineAppendText(Line, " stageText=lowArgReadMapped hardwareArg=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_CMD_WRITTEN:
        A733LineAppendText(Line, " stageText=lowArgReadCmdWritten");
        A733AppendLowArgReadRintInfo(Line, "rintAfterCmd", Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_PIO_DONE:
        A733LineAppendText(Line, " stageText=lowArgReadPioDone");
        A733AppendLowArgReadRintInfo(Line, "rintAfterPio", Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_RINT:
        A733LineAppendText(Line, " stageText=lowArgReadRint");
        A733AppendLowArgReadRintInfo(Line, "rint", Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_FINAL:
        A733LineAppendText(Line, " stageText=lowArgReadFinal");
        A733AppendLowArgReadRintInfo(Line, "finalRint", Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_CMD18_ARG0_DATA_OVER_AS_RESPONSE:
        A733LineAppendText(
            Line,
            " stageText=cmd18Arg0DataOverAsResponse");
        A733AppendLowArgReadRintInfo(Line, "pendingRint", Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_CMD18_ARG0_BLOCK8:
        A733LineAppendText(
            Line,
            " stageText=synthSetupCmd18Arg0Block8 ownerRetired=");
        A733LineAppendDecimal(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_CMD18_ARG0_RESPONSE:
        A733LineAppendText(
            Line,
            " stageText=synthSetupCmd18Arg0Response length=");
        A733LineAppendDecimal(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_CMD18_ARG0_START_DATA:
        A733LineAppendText(
            Line,
            " stageText=synthSetupCmd18Arg0StartData length=");
        A733LineAppendDecimal(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_CMD18_ARG0_SYNC_HANDOFF:
        A733LineAppendText(
            Line,
            " stageText=synthSetupCmd18Arg0SyncHandoff handoffGeneration=");
        A733LineAppendDecimal(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_CMD18_ARG0_FULL_DPC:
        A733LineAppendText(
            Line,
            " stageText=synthSetupCmd18Arg0FullDpc length=");
        A733LineAppendDecimal(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_CMD18_ARG0_FULL_DPC_HANDOFF:
        A733LineAppendText(
            Line,
            " stageText=synthSetupCmd18Arg0FullDpcHandoff length=");
        A733LineAppendDecimal(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_CMD_START_PREWAIT:
        A733LineAppendText(Line, " stageText=cmdStartPrewait cmdReg=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_HLE_RETRY_BEGIN:
        A733LineAppendText(Line, " stageText=readHleRetryBegin rint=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_HLE_RETRY_RESET:
        A733LineAppendText(Line, " stageText=readHleRetryReset rint=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_HLE_RETRY_ISSUE:
        A733LineAppendText(Line, " stageText=readHleRetryIssue arg=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_HLE_RETRY_CMD_DONE:
        A733LineAppendText(Line, " stageText=readHleRetryCmdDone rint=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_HLE_RETRY_RINT_ERROR:
        A733LineAppendText(Line, " stageText=readHleRetryRintError rint=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_HLE_RETRY_PIO:
        A733LineAppendText(Line, " stageText=readHleRetryPio checksum=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_HLE_RETRY_DATA_OVER:
        A733LineAppendText(Line, " stageText=readHleRetryDataOver rint=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_HLE_RETRY_IDLE:
        A733LineAppendText(Line, " stageText=readHleRetryIdle rint=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_HLE_RETRY_RESET_AFTER:
        A733LineAppendText(Line, " stageText=readHleRetryResetAfter rint=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_HLE_RETRY_CMD_DONE_LATE:
        A733LineAppendText(Line, " stageText=readHleRetryCmdDoneLate rint=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_HLE_RETRY_DONE:
        A733LineAppendText(Line, " stageText=readHleRetryDone rint=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_SMALL_READ_FORCE_COMPLETION_RINT:
        A733LineAppendText(
            Line,
            " stageText=smallReadForceCompletionRint finalRint=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_SYNTH_ADVERTISED_TAIL:
        A733LineAppendText(Line, " stageText=synthAdvertisedTailRead arg=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_SETUP_BACKUP_VBR_REMAP:
        A733LineAppendText(Line, " stageText=setupBackupVbrRemap arg=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_BOOT_SECTOR2_LOW_ARG:
        A733LineAppendText(Line, " stageText=synthSetupBootSector2LowArg arg=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_BOOT_SECTOR15_LOW_ARG:
        A733LineAppendText(Line, " stageText=synthSetupBootSector15LowArg arg=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_BACKUP_VBR_LOW_ARG:
        A733LineAppendText(Line, " stageText=synthSetupBackupVbrLowArg arg=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_BOOT16_LOW_ARG0_START:
        A733LineAppendText(Line, " stageText=synthSetupBoot16LowArg0Start length=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_FAT32_ROOT_DIR:
        A733LineAppendText(Line, " stageText=synthSetupFat32RootDir arg=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_FAT32_FAT:
        A733LineAppendText(Line, " stageText=synthSetupFat32Fat arg=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_SYNTH_SETUP_FAT32_SOURCES_DIR:
        A733LineAppendText(Line, " stageText=synthSetupFat32SourcesDir arg=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_SETUP_OPEN_ARM_LIMIT_FAIL:
        A733LineAppendText(Line, " stageText=setupOpenArmLimitFail count=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_ROOT_OPEN_FAIL_ARM:
        A733LineAppendText(Line, " stageText=rootOpenFailArm arg=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_LOGFILE_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=fakeNtfsLogFileWriteBegin"
            " currentLbaSource=transfer.rint");
        A733AppendDiagnosticWriteLbaInfo(Line, Packed);
        break;
    case A733_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_LOGFILE_ABORTED:
        A733LineAppendText(Line, " stageText=fakeNtfsLogFileWriteAbortComplete");
        A733LineAppendText(Line, " rintAfterAbort=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_LOGFILE_SUCCESS:
        A733LineAppendText(Line, " stageText=fakeNtfsLogFileWriteSuccess");
        A733LineAppendText(Line, " checksum=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_WRITE_LBA_PHASE_REDIRECT_LOGFILE_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=redirectNtfsLogFileWriteBegin"
            " originalLbaSource=transfer.rint");
        A733AppendDiagnosticWriteLbaInfo(Line, Packed);
        break;
    case A733_DIAGNOSTIC_WRITE_LBA_PHASE_REDIRECT_LOGFILE_ABORTED:
        A733LineAppendText(
            Line,
            " stageText=redirectNtfsLogFileWriteAbortComplete");
        A733LineAppendText(Line, " rintAfterAbort=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_WRITE_LBA_PHASE_REDIRECT_LOGFILE_BLOCK:
        A733LineAppendText(
            Line,
            " stageText=redirectNtfsLogFileWriteScratchBlock"
            " scratchLbaSource=transfer.rint");
        A733AppendDiagnosticWriteLbaInfo(Line, Packed);
        break;
    case A733_DIAGNOSTIC_WRITE_LBA_PHASE_REDIRECT_LOGFILE_SUCCESS:
        A733LineAppendText(Line, " stageText=redirectNtfsLogFileWriteSuccess");
        A733LineAppendText(Line, " checksum=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_WRITE_LBA_PHASE_NON_LOGFILE_STALL_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=nonLogA733SetupWriteStallBegin"
            " currentLbaSource=transfer.rint");
        A733AppendDiagnosticWriteLbaInfo(Line, Packed);
        break;
    case A733_DIAGNOSTIC_WRITE_LBA_PHASE_NON_LOGFILE_STALL_END:
        A733LineAppendText(
            Line,
            " stageText=nonLogA733SetupWriteStallEnd"
            " currentLbaSource=transfer.rint");
        A733AppendDiagnosticWriteLbaInfo(Line, Packed);
        break;
    case A733_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_NON_LOGFILE_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=fakeNonLogA733SetupWriteBegin"
            " hardwareLbaSource=transfer.rint");
        A733AppendDiagnosticWriteLbaInfo(Line, Packed);
        break;
    case A733_DIAGNOSTIC_WRITE_LBA_PHASE_FAKE_NON_LOGFILE_SUCCESS:
        A733LineAppendText(Line, " stageText=fakeNonLogA733SetupWriteSuccess");
        A733LineAppendText(Line, " checksum=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_BLOCK_BEGIN:
        A733LineAppendText(Line, " stageText=readSplitBlockBegin");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_PIO_DONE:
        A733LineAppendText(Line, " stageText=readSplitPioDone");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_DATA_OVER:
        A733LineAppendText(Line, " stageText=readSplitDataOver");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_DEFERRED_RW_END:
        A733LineAppendText(Line, " stageText=readSplitDeferredRwEnd");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_NEXT_CMD_ISSUE:
        A733LineAppendText(Line, " stageText=readSplitNextCmdIssue");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_NEXT_CMD_DONE:
        A733LineAppendText(Line, " stageText=readSplitNextCmdDone");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_FINAL_QUIESCE:
        A733LineAppendText(Line, " stageText=readSplitFinalQuiesce");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK10_HOLD_BEGIN:
        A733LineAppendText(Line, " stageText=readSplitLba0Block10HoldBegin");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK10_HOLD_END:
        A733LineAppendText(Line, " stageText=readSplitLba0Block10HoldEnd");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK5_DATA_OVER_HOLD_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block5DataOverHoldBegin");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK5_DATA_OVER_HOLD_END:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block5DataOverHoldEnd");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK7_DATA_OVER_HOLD_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block7DataOverHoldBegin");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK7_DATA_OVER_HOLD_END:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block7DataOverHoldEnd");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK49_DATA_OVER_HOLD_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block49DataOverHoldBegin");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK49_DATA_OVER_HOLD_END:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block49DataOverHoldEnd");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK36_BEGIN_HOLD_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block36BeginHoldBegin");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK36_BEGIN_HOLD_END:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block36BeginHoldEnd");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK60_PIO_HOLD_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block60PioHoldBegin");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK60_PIO_HOLD_END:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block60PioHoldEnd");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK2_PIO_HOLD_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block2PioHoldBegin");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK2_PIO_HOLD_END:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block2PioHoldEnd");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK55_NEXT_CMD_HOLD_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block55NextCmdHoldBegin");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK55_NEXT_CMD_HOLD_END:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block55NextCmdHoldEnd");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK27_NEXT_CMD_HOLD_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block27NextCmdHoldBegin");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK27_NEXT_CMD_HOLD_END:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block27NextCmdHoldEnd");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK27_PIO_HOLD_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block27PioHoldBegin");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK27_PIO_HOLD_END:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block27PioHoldEnd");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK1_BEGIN_HOLD_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block1BeginHoldBegin");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK1_BEGIN_HOLD_END:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block1BeginHoldEnd");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK8_PIO_HOLD_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block8PioHoldBegin");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK8_PIO_HOLD_END:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block8PioHoldEnd");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK17_NEXT_CMD_HOLD_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block17NextCmdHoldBegin");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK17_NEXT_CMD_HOLD_END:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block17NextCmdHoldEnd");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_A733SETUP_BLOCK12_PIO_HOLD_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=readSplitA733SetupBlock12PioHoldBegin");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_A733SETUP_BLOCK12_PIO_HOLD_END:
        A733LineAppendText(
            Line,
            " stageText=readSplitA733SetupBlock12PioHoldEnd");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK38_POST_DATAOVER_HOLD_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block38PostDataOverHoldBegin");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK38_POST_DATAOVER_HOLD_END:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block38PostDataOverHoldEnd");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK40_NEXT_CMD_DONE_HOLD_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block40NextCmdDoneHoldBegin");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK40_NEXT_CMD_DONE_HOLD_END:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block40NextCmdDoneHoldEnd");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_ARG2_BLOCK29_PIO_HOLD_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=readSplitArg2Block29PioHoldBegin");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_ARG2_BLOCK29_PIO_HOLD_END:
        A733LineAppendText(
            Line,
            " stageText=readSplitArg2Block29PioHoldEnd");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK92_DATA_OVER_HOLD_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block92DataOverHoldBegin");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK92_DATA_OVER_HOLD_END:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block92DataOverHoldEnd");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK95_NEXT_CMD_HOLD_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block95NextCmdHoldBegin");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK95_NEXT_CMD_HOLD_END:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block95NextCmdHoldEnd");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK33_DATA_OVER_HOLD_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block33DataOverHoldBegin");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK33_DATA_OVER_HOLD_END:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block33DataOverHoldEnd");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK35_DATA_OVER_HOLD_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block35DataOverHoldBegin");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK35_DATA_OVER_HOLD_END:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block35DataOverHoldEnd");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK59_NEXT_CMD_HOLD_BEGIN:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block59NextCmdHoldBegin");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK59_NEXT_CMD_HOLD_END:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block59NextCmdHoldEnd");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK59_NEXT_CMD_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block59NextCmdBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK59_NEXT_CMD_DONE_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block59NextCmdDoneBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK59_DATA_OVER_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block59DataOverBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK60_NEXT_CMD_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block60NextCmdBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK60_NEXT_CMD_DONE_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block60NextCmdDoneBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK60_DATA_OVER_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block60DataOverBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK61_NEXT_CMD_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block61NextCmdBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK61_NEXT_CMD_DONE_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block61NextCmdDoneBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK61_DATA_OVER_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block61DataOverBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK62_NEXT_CMD_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block62NextCmdBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK62_NEXT_CMD_DONE_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block62NextCmdDoneBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK62_DATA_OVER_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block62DataOverBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK63_NEXT_CMD_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block63NextCmdBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK63_NEXT_CMD_DONE_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block63NextCmdDoneBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK63_DATA_OVER_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block63DataOverBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK64_NEXT_CMD_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block64NextCmdBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK64_NEXT_CMD_DONE_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block64NextCmdDoneBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK64_DATA_OVER_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block64DataOverBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK65_NEXT_CMD_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block65NextCmdBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK65_NEXT_CMD_DONE_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block65NextCmdDoneBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK65_DATA_OVER_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block65DataOverBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK66_NEXT_CMD_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block66NextCmdBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK66_NEXT_CMD_DONE_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block66NextCmdDoneBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK66_DATA_OVER_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block66DataOverBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK67_NEXT_CMD_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block67NextCmdBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK67_NEXT_CMD_DONE_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block67NextCmdDoneBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK67_DATA_OVER_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block67DataOverBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK68_NEXT_CMD_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block68NextCmdBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK68_NEXT_CMD_DONE_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block68NextCmdDoneBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK68_DATA_OVER_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block68DataOverBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK69_NEXT_CMD_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block69NextCmdBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK69_NEXT_CMD_DONE_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block69NextCmdDoneBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK69_DATA_OVER_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block69DataOverBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK70_NEXT_CMD_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block70NextCmdBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK70_NEXT_CMD_DONE_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block70NextCmdDoneBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK70_DATA_OVER_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block70DataOverBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK71_NEXT_CMD_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block71NextCmdBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK71_NEXT_CMD_DONE_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block71NextCmdDoneBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK71_DATA_OVER_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block71DataOverBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK72_NEXT_CMD_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block72NextCmdBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK72_NEXT_CMD_DONE_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block72NextCmdDoneBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK72_DATA_OVER_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block72DataOverBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK73_NEXT_CMD_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block73NextCmdBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK73_NEXT_CMD_DONE_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block73NextCmdDoneBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK73_DATA_OVER_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block73DataOverBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK74_NEXT_CMD_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block74NextCmdBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK74_NEXT_CMD_DONE_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block74NextCmdDoneBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LBA0_BLOCK74_DATA_OVER_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitLba0Block74DataOverBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_TARGET_NEXT_CMD_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitTargetNextCmdBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_TARGET_NEXT_CMD_DONE_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitTargetNextCmdDoneBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_TARGET_DATA_OVER_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitTargetDataOverBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_FINAL_LOOP_EXIT_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitFinalLoopExitBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_FINAL_RESET_IDLE_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitFinalResetIdleBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_FINAL_QUIESCE_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitFinalQuiesceBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_FINAL_RETURN_BUGCHECK:
        A733LineAppendText(
            Line,
            " stageText=readSplitFinalReturnBugcheck");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_SYNTH_DATA_OVER_IDLE:
        A733LineAppendText(
            Line,
            " stageText=readSplitSynthDataOverIdle");
        A733AppendReadSplitProgressInfo(Line, Payload, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_DATA_OVER_TIMEOUT_STATE:
        A733LineAppendText(
            Line,
            " stageText=readSplitDataOverTimeoutState statusReg=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_DATA_OVER_RESET_PATH:
        A733LineAppendText(
            Line,
            " stageText=readSplitDataOverResetPath statusReg=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_PIO_TIMEOUT_STATE:
        A733LineAppendText(
            Line,
            " stageText=readSplitPioTimeoutState statusReg=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_REDIRECT_SCRATCH_WRITE_RESET:
        A733LineAppendText(
            Line,
            " stageText=redirectScratchWriteResetFifo rintAfterReset=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_REDIRECT_SCRATCH_WRITE_ISSUE:
        A733LineAppendText(
            Line,
            " stageText=redirectScratchWriteIssueCmd scratchLba=");
        A733LineAppendHex32(Line, Packed);
        A733LineAppendText(Line, " scratchLbaDec=");
        A733LineAppendDecimal(Line, Packed);
        break;
    case A733_DIAGNOSTIC_REDIRECT_SCRATCH_WRITE_CMD_DONE:
        A733LineAppendText(
            Line,
            " stageText=redirectScratchWriteStartReady rint=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_REDIRECT_SCRATCH_WRITE_RINT_ERROR:
        A733LineAppendText(
            Line,
            " stageText=redirectScratchWriteRintError rint=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_REDIRECT_SCRATCH_WRITE_PIO:
        A733LineAppendText(
            Line,
            " stageText=redirectScratchWritePioWrite checksum=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_REDIRECT_SCRATCH_WRITE_POST_PIO_READY:
        A733LineAppendText(
            Line,
            " stageText=redirectScratchWritePostPioReady rint=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_REDIRECT_SCRATCH_WRITE_DATA_OVER:
        A733LineAppendText(
            Line,
            " stageText=redirectScratchWriteDataOver rint=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_REDIRECT_SCRATCH_WRITE_IDLE:
        A733LineAppendText(
            Line,
            " stageText=redirectScratchWriteDataPathIdle rint=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_REDIRECT_SCRATCH_WRITE_RESET_AFTER:
        A733LineAppendText(
            Line,
            " stageText=redirectScratchWriteResetFifoAfter rint=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_REDIRECT_SCRATCH_READ_RESET:
        A733LineAppendText(
            Line,
            " stageText=redirectScratchReadResetFifo rintAfterReset=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_REDIRECT_SCRATCH_READ_ISSUE:
        A733LineAppendText(
            Line,
            " stageText=redirectScratchReadIssueCmd scratchLba=");
        A733LineAppendHex32(Line, Packed);
        A733LineAppendText(Line, " scratchLbaDec=");
        A733LineAppendDecimal(Line, Packed);
        break;
    case A733_DIAGNOSTIC_REDIRECT_SCRATCH_READ_CMD_DONE:
        A733LineAppendText(
            Line,
            " stageText=redirectScratchReadCmdDone rint=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_REDIRECT_SCRATCH_READ_RINT_ERROR:
        A733LineAppendText(
            Line,
            " stageText=redirectScratchReadRintError rint=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_REDIRECT_SCRATCH_READ_PIO:
        A733LineAppendText(
            Line,
            " stageText=redirectScratchReadPioRead checksum=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_REDIRECT_SCRATCH_READ_DATA_OVER:
        A733LineAppendText(
            Line,
            " stageText=redirectScratchReadDataOver rint=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_REDIRECT_SCRATCH_READ_IDLE:
        A733LineAppendText(
            Line,
            " stageText=redirectScratchReadDataPathIdle rint=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_REDIRECT_SCRATCH_READ_RESET_AFTER:
        A733LineAppendText(
            Line,
            " stageText=redirectScratchReadResetFifoAfter rint=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_REDIRECT_SCRATCH_VERIFY_MISMATCH:
        A733LineAppendText(
            Line,
            " stageText=redirectScratchVerifyMismatch mismatchDetail=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_REDIRECT_SCRATCH_VERIFY_EXPECTED:
        A733LineAppendText(
            Line,
            " stageText=redirectScratchVerifyExpectedChecksum checksum=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_REDIRECT_SCRATCH_VERIFY_ACTUAL:
        A733LineAppendText(
            Line,
            " stageText=redirectScratchVerifyActualChecksum checksum=");
        A733LineAppendHex32(Line, Packed);
        break;
    case A733_DIAGNOSTIC_REDIRECT_SCRATCH_VERIFY_SUCCESS:
        A733LineAppendText(
            Line,
            " stageText=redirectScratchVerifySuccess checksum=");
        A733LineAppendHex32(Line, Packed);
        break;
    default:
        break;
    }
}

static VOID
A733AppendLifecycleDecode(
    _Inout_ A733_LINE *Line,
    _In_reads_bytes_(6U * sizeof(ULONG)) const BYTE *Payload)
{
    ULONG Stage;
    ULONG Status;
    ULONG A;
    ULONG B;
    ULONG C;
    ULONG D;
    ULONG BlockCount;
    ULONG BlockSize;
    ULONG PackedResponseTransferReset;
    ULONG RawState;
    ULONG NormalizedState;

    Stage = A733ReadPackedUlong(Payload);
    Status = A733ReadPackedUlong(Payload + sizeof(ULONG));
    A = A733ReadPackedUlong(Payload + (2U * sizeof(ULONG)));
    B = A733ReadPackedUlong(Payload + (3U * sizeof(ULONG)));
    C = A733ReadPackedUlong(Payload + (4U * sizeof(ULONG)));
    D = A733ReadPackedUlong(Payload + (5U * sizeof(ULONG)));

    if ((Stage >= 60U) && (Stage <= 65U)) {
        A733LineAppendText(Line, " stageText=resetSnapshot resetTypeName=");
        A733LineAppendText(Line, A733ResetTypeName(A));
        if ((Stage == 60U) || (Stage == 61U)) {
            A733LineAppendText(Line, " detailName=resetMask");
        } else {
            A733LineAppendText(Line, " detailName=clockKhz");
        }
        A733LineAppendText(Line, " snapshotFlags=");
        A733LineAppendHex32(Line, C);
        A733AppendEventMaskDecode(Line, D);
        return;
    }

    switch (Stage) {
    case 40U:
        A733LineAppendText(Line, " stageText=issueRequestEnter requestTypeName=");
        A733LineAppendText(Line, A733RequestTypeName(A));
        A733LineAppendText(Line, " cmdName=");
        A733LineAppendText(Line, A733SdCommandName(B));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, B);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, B);
        A733AppendEventMaskDecode(Line, D);
        break;
    case 41U:
        A733LineAppendText(Line, " stageText=issueRequestBusy requestTypeName=");
        A733LineAppendText(Line, A733RequestTypeName(A));
        A733LineAppendText(Line, " cmdName=");
        A733LineAppendText(Line, A733SdCommandName(B));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, B);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, B);
        A733AppendEventMaskDecode(Line, D);
        break;
    case 42U:
        A733LineAppendText(Line, " stageText=issueRequestExit requestTypeName=");
        A733LineAppendText(Line, A733RequestTypeName(A));
        A733LineAppendText(Line, " cmdName=");
        A733LineAppendText(Line, A733SdCommandName(B));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, B);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, B);
        A733AppendEventMaskDecode(Line, D);
        break;
    case 43U:
        A733LineAppendText(Line, " stageText=startTransferReturnSuccess requestTypeName=");
        A733LineAppendText(Line, A733RequestTypeName(A));
        A733LineAppendText(Line, " cmdName=");
        A733LineAppendText(Line, A733SdCommandName(B));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, B);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, B);
        A733AppendEventMaskDecode(Line, D);
        break;
    case 44U:
        A733LineAppendText(Line, " stageText=startTransferPreCompleteRequest requestTypeName=");
        A733LineAppendText(Line, A733RequestTypeName(A));
        A733LineAppendText(Line, " cmdName=");
        A733LineAppendText(Line, A733SdCommandName(B));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, B);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, B);
        A733AppendEventMaskDecode(Line, D);
        break;
    case 45U:
        A733LineAppendText(Line, " stageText=startTransferPostCompleteRequest requestTypeName=");
        A733LineAppendText(Line, A733RequestTypeName(A));
        A733LineAppendText(Line, " cmdName=");
        A733LineAppendText(Line, A733SdCommandName(B));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, B);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, B);
        A733AppendEventMaskDecode(Line, D);
        break;
    case A733_DIAGNOSTIC_PHASE_CMD17_ARG0_ISSUE_HOLD_BEGIN:
        A733LineAppendText(Line, " stageText=cmd17Arg0IssueHoldBegin requestTypeName=");
        A733LineAppendText(Line, A733RequestTypeName(A));
        A733LineAppendText(Line, " cmdName=");
        A733LineAppendText(Line, A733SdCommandName(B));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, B);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " cmdArgument=");
        A733LineAppendHex32(Line, C);
        A733AppendEventMaskDecode(Line, D);
        break;
    case A733_DIAGNOSTIC_PHASE_CMD17_ARG0_ISSUE_HOLD_END:
        A733LineAppendText(Line, " stageText=cmd17Arg0IssueHoldEnd requestTypeName=");
        A733LineAppendText(Line, A733RequestTypeName(A));
        A733LineAppendText(Line, " cmdName=");
        A733LineAppendText(Line, A733SdCommandName(B));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, B);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " cmdArgument=");
        A733LineAppendHex32(Line, C);
        A733AppendEventMaskDecode(Line, D);
        break;
    case A733_DIAGNOSTIC_PHASE_CMD17_ARG21_ISSUE_HOLD_BEGIN:
        A733LineAppendText(Line, " stageText=cmd17Arg21IssueHoldBegin requestTypeName=");
        A733LineAppendText(Line, A733RequestTypeName(A));
        A733LineAppendText(Line, " cmdName=");
        A733LineAppendText(Line, A733SdCommandName(B));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, B);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " cmdArgument=");
        A733LineAppendHex32(Line, C);
        A733AppendEventMaskDecode(Line, D);
        break;
    case A733_DIAGNOSTIC_PHASE_CMD17_ARG21_ISSUE_HOLD_END:
        A733LineAppendText(Line, " stageText=cmd17Arg21IssueHoldEnd requestTypeName=");
        A733LineAppendText(Line, A733RequestTypeName(A));
        A733LineAppendText(Line, " cmdName=");
        A733LineAppendText(Line, A733SdCommandName(B));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, B);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " cmdArgument=");
        A733LineAppendHex32(Line, C);
        A733AppendEventMaskDecode(Line, D);
        break;
    case A733_DIAGNOSTIC_PHASE_CMD17_ARG10000_ISSUE_HOLD_BEGIN:
        A733LineAppendText(Line, " stageText=cmd17Arg10000IssueHoldBegin requestTypeName=");
        A733LineAppendText(Line, A733RequestTypeName(A));
        A733LineAppendText(Line, " cmdName=");
        A733LineAppendText(Line, A733SdCommandName(B));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, B);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " cmdArgument=");
        A733LineAppendHex32(Line, C);
        A733AppendEventMaskDecode(Line, D);
        break;
    case A733_DIAGNOSTIC_PHASE_CMD17_ARG10000_ISSUE_HOLD_END:
        A733LineAppendText(Line, " stageText=cmd17Arg10000IssueHoldEnd requestTypeName=");
        A733LineAppendText(Line, A733RequestTypeName(A));
        A733LineAppendText(Line, " cmdName=");
        A733LineAppendText(Line, A733SdCommandName(B));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, B);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " cmdArgument=");
        A733LineAppendHex32(Line, C);
        A733AppendEventMaskDecode(Line, D);
        break;
    case 46U:
        A733LineAppendText(Line, " stageText=writeSuccessAlternateCompletion requestTypeName=");
        A733LineAppendText(Line, A733RequestTypeName(A));
        A733LineAppendText(Line, " cmdName=");
        A733LineAppendText(Line, A733SdCommandName(B));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, B);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, B);
        A733AppendEventMaskDecode(Line, D);
        break;
    case 47U:
        A733LineAppendText(Line, " stageText=deferredCompleteEnter requestTypeName=");
        A733LineAppendText(Line, A733RequestTypeName(A));
        A733LineAppendText(Line, " cmdName=");
        A733LineAppendText(Line, A733SdCommandName(B));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, B);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, B);
        A733AppendEventMaskDecode(Line, D);
        break;
    case 48U:
        A733LineAppendText(Line, " stageText=deferredCompleteExit requestTypeName=");
        A733LineAppendText(Line, A733RequestTypeName(A));
        A733LineAppendText(Line, " cmdName=");
        A733LineAppendText(Line, A733SdCommandName(B));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, B);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, B);
        A733AppendEventMaskDecode(Line, D);
        break;
    case 49U:
        A733LineAppendText(Line, " stageText=deferredCompleteError requestTypeName=");
        A733LineAppendText(Line, A733RequestTypeName(A));
        A733LineAppendText(Line, " cmdName=");
        A733LineAppendText(Line, A733SdCommandName(B));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, B);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, B);
        A733AppendEventMaskDecode(Line, D);
        break;
    case 52U:
        A733LineAppendText(Line, " stageText=sampleLikeWriteCompletionFields requestTypeName=");
        A733LineAppendText(Line, A733RequestTypeName(A));
        A733LineAppendText(Line, " cmdName=");
        A733LineAppendText(Line, A733SdCommandName(B));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, B);
        A733LineAppendText(Line, " originalLength=");
        A733LineAppendDecimal(Line, C);
        A733LineAppendText(Line, " originalBlockCount=");
        A733LineAppendDecimal(Line, D);
        break;
    case 53U:
        A733LineAppendText(Line, " stageText=writeSuccessRequestDpcPreComplete requestTypeName=");
        A733LineAppendText(Line, A733RequestTypeName(A));
        A733LineAppendText(Line, " cmdName=");
        A733LineAppendText(Line, A733SdCommandName(B));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, B);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, B);
        A733AppendEventMaskDecode(Line, D);
        break;
    case 54U:
        A733LineAppendText(Line, " stageText=writeSuccessRequestDpcPostComplete requestTypeName=");
        A733LineAppendText(Line, A733RequestTypeName(A));
        A733LineAppendText(Line, " cmdName=");
        A733LineAppendText(Line, A733SdCommandName(B));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, B);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, B);
        A733AppendEventMaskDecode(Line, D);
        break;
    case 55U:
        A733LineAppendText(Line, " stageText=writeCompletionTicketQueued ticket=");
        A733LineAppendDecimal(Line, A);
        A733LineAppendText(Line, " requestLow=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " activeLow=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " generationPack=");
        A733LineAppendHex32(Line, D);
        break;
    case 56U:
        A733LineAppendText(Line, " stageText=writeCompletionTicketTimerDpc ticket=");
        A733LineAppendDecimal(Line, A);
        A733LineAppendText(Line, " requestLow=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " activeLow=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " generationPack=");
        A733LineAppendHex32(Line, D);
        break;
    case 57U:
        A733LineAppendText(Line, " stageText=writeCompletionTicketRequestDpcPreComplete ticket=");
        A733LineAppendDecimal(Line, A);
        A733LineAppendText(Line, " requestLow=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " activeLow=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " generationPack=");
        A733LineAppendHex32(Line, D);
        break;
    case 58U:
        A733LineAppendText(Line, " stageText=writeCompletionTicketRequestDpcPostComplete ticket=");
        A733LineAppendDecimal(Line, A);
        A733LineAppendText(Line, " requestLow=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " activeLow=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " generationPack=");
        A733LineAppendHex32(Line, D);
        break;
    case 89U:
        A733LineAppendText(Line, " stageText=moduleHintBatch batch=");
        A733LineAppendDecimal(Line, A);
        break;
    case 90U:
        A733LineAppendText(Line, " stageText=moduleHintBase tagName=");
        A733LineAppendText(Line, A733ModuleHintName(A));
        A733LineAppendText(Line, " base=");
        A733LineAppendHex64(Line, ((ULONGLONG)C << 32U) | B);
        A733LineAppendText(Line, " size=");
        A733LineAppendHex32(Line, D);
        break;
    case 91U:
        A733LineAppendText(Line, " stageText=moduleHintPc tagName=");
        A733LineAppendText(Line, A733ModuleHintName(A));
        A733LineAppendText(Line, " pc=");
        A733LineAppendHex64(Line, ((ULONGLONG)C << 32U) | B);
        A733LineAppendText(Line, " offset=");
        A733LineAppendHex32(Line, D);
        break;
    case 92U:
        A733LineAppendText(Line, " stageText=cmd9R2Raw sourceName=");
        if (Status == 2) {
            A733LineAppendText(Line, "cachedCommandDone");
        } else if (Status == 1) {
            A733LineAppendText(Line, "registerRead");
        } else {
            A733LineAppendText(Line, "none");
        }
        A733LineAppendText(Line, " raw0=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " raw1=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " raw2=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " raw3=");
        A733LineAppendHex32(Line, D);
        break;
    case 93U:
        A733LineAppendText(Line, " stageText=cmd9R2Out orderName=sunxiReverse out0=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " out1=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " out2=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " out3=");
        A733LineAppendHex32(Line, D);
        break;
    case 94U:
        A733LineAppendText(Line, " stageText=syntheticAcmd51ScrWords sourceName=");
        if (A == 1U) {
            A733LineAppendText(Line, "issueCommand");
        } else if (A == 2U) {
            A733LineAppendText(Line, "startTransfer");
        } else {
            A733LineAppendText(Line, "unknown");
        }
        A733LineAppendText(Line, " data0BytesLE=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " data1BytesLE=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " sdportArgument=");
        A733LineAppendHex32(Line, D);
        break;
    case 95U:
        A733LineAppendText(Line, " stageText=acmdR1AppCmdClear cmdName=");
        A733LineAppendText(Line, A733SdCommandName(A));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, A);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " responseName=");
        A733LineAppendText(Line, A733ResponseTypeName(D));
        A733LineAppendText(Line, " rawResponse=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " normalizedResponse=");
        A733LineAppendHex32(Line, C);
        break;
    case 96U:
        A733LineAppendText(Line, " stageText=cmd2R2Raw sourceName=");
        if (Status == 2) {
            A733LineAppendText(Line, "cachedCommandDone");
        } else if (Status == 1) {
            A733LineAppendText(Line, "registerRead");
        } else {
            A733LineAppendText(Line, "none");
        }
        A733LineAppendText(Line, " raw0=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " raw1=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " raw2=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " raw3=");
        A733LineAppendHex32(Line, D);
        break;
    case 97U:
        A733LineAppendText(Line, " stageText=cmd2R2Out orderName=sunxiReverse out0=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " out1=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " out2=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " out3=");
        A733LineAppendHex32(Line, D);
        break;
    case 98U:
        A733LineAppendText(Line, " stageText=cmd6R1ForceTran sdportArgument=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " rawResponse=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " normalizedResponse=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " responseType=");
        A733LineAppendHex32(Line, D);
        break;
    case 99U:
        A733LineAppendText(Line, " stageText=cmd6IgnoredCommandErrors sdportArgument=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " ignoredErrors=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " requiredEventsAfter=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " deferredEventsAfter=");
        A733LineAppendHex32(Line, D);
        break;
    case 100U:
        A733LineAppendText(Line, " stageText=cmd6IssuePlan commandValue=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " hardwareArgument=");
        A733LineAppendHex32(Line, B);
        A733AppendCommandShapeDecode(Line, C);
        A733AppendTransferShapeDecode(Line, D);
        break;
    case 101U:
        A733LineAppendText(Line, " stageText=cmd6IssueEvents");
        A733AppendEventMaskDecode(Line, A);
        A733LineAppendText(Line, " enabledRint=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " transferCompleteRint=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " snapshotFlags=");
        A733LineAppendHex32(Line, D);
        break;
    case 102U:
        A733LineAppendText(Line, " stageText=cmd6IssueRegs cmdReg=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " argReg=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " rint=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " statusReg=");
        A733LineAppendHex32(Line, D);
        break;
    case 103U:
        A733LineAppendText(Line, " stageText=cmd6IssueRespIrq resp0=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " resp1=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " gctrl=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " imask=");
        A733LineAppendHex32(Line, D);
        break;
    case 104U:
        A733LineAppendText(Line, " stageText=cmd6IssueDataRegs requestStatus=");
        A733LineAppendHex32(Line, Status);
        A733LineAppendText(Line, " blksz=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " bytcnt=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " deferredEvents=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " deferredErrors=");
        A733LineAppendHex32(Line, D);
        break;
    case 105U:
        A733LineAppendText(
            Line,
            " stageText=cmd6SyntheticStatus cmdName=");
        A733LineAppendText(Line, A733SdCommandName(6U));
        A733LineAppendText(Line, " sdportArgument=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " statusBytes12_15LE=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " statusBytes16_19LE=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " statusBytes28_31LE=");
        A733LineAppendHex32(Line, D);
        break;
    case 106U:
        A733LineAppendText(
            Line,
            " stageText=cmd6DeferredQueueResult cmdName=");
        A733LineAppendText(Line, A733SdCommandName(6U));
        A733LineAppendText(Line, " sdportArgument=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " ticket=");
        A733LineAppendDecimal(Line, B);
        A733LineAppendText(Line, " activeGeneration=");
        A733LineAppendDecimal(Line, C & 0x0000FFFFUL);
        A733LineAppendText(Line, " dpcGeneration=");
        A733LineAppendDecimal(Line, (C >> 16U) & 0x0000FFFFUL);
        A733LineAppendText(Line, " requiredEvents=");
        A733LineAppendHex32(Line, D & 0x0000FFFFUL);
        A733AppendEventMaskDecode(Line, D & 0x0000FFFFUL);
        A733LineAppendText(Line, " snapshotFlags=");
        A733LineAppendHex32(Line, (D >> 16U) & 0x0000FFFFUL);
        break;
    case 107U:
        A733LineAppendText(
            Line,
            " stageText=cmd6PreSdPortComplete cmdName=");
        A733LineAppendText(Line, A733SdCommandName(6U));
        A733LineAppendText(Line, " ticket=");
        A733LineAppendDecimal(Line, A);
        A733LineAppendText(Line, " requestTypeName=");
        A733LineAppendText(Line, A733RequestTypeName(B));
        A733LineAppendText(Line, " sdportArgument=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " requiredEvents=");
        A733LineAppendHex32(Line, D & 0x0000FFFFUL);
        A733AppendEventMaskDecode(Line, D & 0x0000FFFFUL);
        A733LineAppendText(Line, " snapshotFlags=");
        A733LineAppendHex32(Line, (D >> 16U) & 0x0000FFFFUL);
        break;
    case 108U:
        A733LineAppendText(
            Line,
            " stageText=cmd6PostSdPortComplete cmdName=");
        A733LineAppendText(Line, A733SdCommandName(6U));
        A733LineAppendText(Line, " ticket=");
        A733LineAppendDecimal(Line, A);
        A733LineAppendText(Line, " requestTypeName=");
        A733LineAppendText(Line, A733RequestTypeName(B));
        A733LineAppendText(Line, " sdportArgument=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " requiredEvents=");
        A733LineAppendHex32(Line, D & 0x0000FFFFUL);
        A733AppendEventMaskDecode(Line, D & 0x0000FFFFUL);
        A733LineAppendText(Line, " snapshotFlags=");
        A733LineAppendHex32(Line, (D >> 16U) & 0x0000FFFFUL);
        break;
    case 109U:
        A733LineAppendText(
            Line,
            " stageText=cmd6DpcIdentity cmdName=");
        A733LineAppendText(Line, A733SdCommandName(6U));
        A733LineAppendText(Line, " dpcGeneration=");
        A733LineAppendDecimal(Line, A);
        A733LineAppendText(Line, " activeGeneration=");
        A733LineAppendDecimal(Line, B);
        A733LineAppendText(Line, " activeRequestLow=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " requestLow=");
        A733LineAppendHex32(Line, D);
        break;
    case 110U:
        A733LineAppendText(
            Line,
            " stageText=cmd6StartTransferAlreadyComplete requestTypeName=");
        A733LineAppendText(Line, A733RequestTypeName(A));
        A733LineAppendText(Line, " cmdName=");
        A733LineAppendText(Line, A733SdCommandName(B));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, B);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " sdportArgument=");
        A733LineAppendHex32(Line, C);
        A733AppendEventMaskDecode(Line, D);
        break;
    case 111U:
        A733LineAppendText(
            Line,
            " stageText=setClockAcceptedNoHwUpdate requestedKhz=");
        A733LineAppendDecimal(Line, A);
        A733LineAppendText(Line, " currentClockKhz=");
        A733LineAppendDecimal(Line, B);
        A733LineAppendText(Line, " clkcr=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " statusReg=");
        A733LineAppendHex32(Line, D);
        break;
    case 112U:
        A733LineAppendText(Line, " stageText=cmd9CsdCapacityPatch oldWord=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " newWord=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " expectedCSize=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " response0=");
        A733LineAppendHex32(Line, D);
        break;
    case 113U:
        A733LineAppendText(Line, " stageText=cmd9CsdWriteProtectClear oldWord=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " newWord=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " clearedMask=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " response3=");
        A733LineAppendHex32(Line, D);
        break;
    case 66U:
        PackedResponseTransferReset = D;
        A733LineAppendText(Line, " stageText=lastCommandForReset requestTypeName=");
        A733LineAppendText(Line, A733RequestTypeName(A));
        A733LineAppendText(Line, " cmdName=");
        A733LineAppendText(Line, A733SdCommandName(B));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, B);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " responseName=");
        A733LineAppendText(Line, A733ResponseTypeName(
                           PackedResponseTransferReset & 0xFFU));
        A733LineAppendText(Line, " transferTypeName=");
        A733LineAppendText(Line, A733TransferTypeName(
                           (PackedResponseTransferReset >> 8U) & 0xFFU));
        A733LineAppendText(Line, " transferMethodName=");
        A733LineAppendText(Line, A733TransferMethodName(
                           (PackedResponseTransferReset >> 16U) & 0xFFU));
        A733LineAppendText(Line, " resetTypeName=");
        A733LineAppendText(Line, A733ResetTypeName(
                           (PackedResponseTransferReset >> 24U) & 0xFFU));
        break;
    case 67U:
        A733LineAppendText(Line, " stageText=lastCommandRegs commandValue=");
        A733LineAppendHex32(Line, A);
        A733AppendEventMaskDecode(Line, B);
        break;
    case 68U:
        BlockSize = C & 0xFFFFU;
        BlockCount = (C >> 16U) & 0xFFFFU;
        A733LineAppendText(Line, " stageText=smallReadRequest cmdName=");
        A733LineAppendText(Line, A733SdCommandName(A));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, A);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " blockCount=");
        A733LineAppendDecimal(Line, BlockCount);
        A733LineAppendText(Line, " blockSize=");
        A733LineAppendDecimal(Line, BlockSize);
        break;
    case 69U:
        A733LineAppendText(Line, " stageText=smallReadData data0BytesLE=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " data1BytesLE=");
        A733LineAppendHex32(Line, B);
        break;
    case 70U:
        A733LineAppendText(Line, " stageText=smallReadPostCompletion requestStatus=");
        A733LineAppendHex32(Line, A);
        A733AppendEventMaskDecode(Line, B);
        A733LineAppendText(Line, " rintAfter=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " finalRint=");
        A733LineAppendHex32(Line, D);
        break;
    case 71U:
        A733LineAppendText(Line, " stageText=smallReadCompletionState");
        A733AppendEventMaskDecode(Line, A);
        A733LineAppendText(Line, " transferCompleteRint=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " deferredEvents=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " deferredErrors=");
        A733LineAppendHex32(Line, D);
        break;
    case 72U:
        A733LineAppendText(Line, " stageText=smallReadShapeAndRegs");
        A733AppendCommandShapeDecode(Line, A);
        A733AppendTransferShapeDecode(Line, B);
        break;
    case 73U:
        A733LineAppendText(Line, " stageText=smallReadPostRegs");
        break;
    case A733_DIAGNOSTIC_READ_PHASE_LOW_ARG_TRACE_DATA:
        A733LineAppendText(Line, " stageText=lowArgReadData hardwareArg=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " data0BytesLE=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " data1BytesLE=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " checksum=");
        A733LineAppendHex32(Line, D);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_MFT_RECORD_DATA:
        A733LineAppendText(Line, " stageText=readSplitMftRecordData currentArg=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " data0BytesLE=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " data1BytesLE=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " checksum=");
        A733LineAppendHex32(Line, D);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_MFT_ARG_MAP:
        A733LineAppendText(Line, " stageText=readSplitMftArgMap currentArg=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " hardwareArg=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " blockIndex=");
        A733LineAppendDecimal(Line, C);
        A733LineAppendText(Line, " blocksRemaining=");
        A733LineAppendDecimal(Line, D);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_MFT_RECORD3_VOLUME_FLAGS_PATCH:
        A733LineAppendText(Line, " stageText=mftRecord3VolumeFlagsPatch lba=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " oldFlags=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " newFlags=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " byteCount=");
        A733LineAppendDecimal(Line, D);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LOGFILE_RESTART_DATA:
        A733LineAppendText(Line, " stageText=readSplitLogFileRestartData currentArg=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " data0BytesLE=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " data1BytesLE=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " checksum=");
        A733LineAppendHex32(Line, D);
        break;
    case A733_DIAGNOSTIC_READ_SPLIT_PHASE_LOGFILE_OVERLAY_DATA:
        A733LineAppendText(Line, " stageText=readSplitLogFileOverlayData currentArg=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " data0BytesLE=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " data1BytesLE=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " checksum=");
        A733LineAppendHex32(Line, D);
        break;
    case A733_DIAGNOSTIC_READ_PHASE_PARTITION_RELATIVE_MODE:
        A733LineAppendText(Line, " stageText=partitionRelativeMode sector=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " hardwareSector=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " requestArg=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " blockCount=");
        A733LineAppendDecimal(Line, D);
        break;
    case 74U:
        A733LineAppendText(Line, " stageText=lastResponseLow cmdName=");
        A733LineAppendText(Line, A733SdCommandName(A));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, A);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " responseName=");
        A733LineAppendText(Line, A733ResponseTypeName(B));
        if (A733IsR1LikeResponse(B)) {
            A733AppendR1Decode(Line, C);
        }
        break;
    case 75U:
        A733LineAppendText(Line, " stageText=lastResponseHigh cmdName=");
        A733LineAppendText(Line, A733SdCommandName(A));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, A);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " responseName=");
        A733LineAppendText(Line, A733ResponseTypeName(B));
        break;
    case 76U:
        A733LineAppendText(Line, " stageText=lastCommandDetail requestTypeName=");
        A733LineAppendText(Line, A733RequestTypeName(A));
        A733LineAppendText(Line, " cmdName=");
        A733LineAppendText(Line, A733SdCommandName(B));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, B);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, B);
        A733AppendCommandShapeDecode(Line, C);
        A733AppendTransferShapeDecode(Line, D);
        break;
    case 77U:
        A733LineAppendText(Line, " stageText=lastCommandDetailEvents requestStatus=");
        A733LineAppendHex32(Line, Status);
        A733AppendEventMaskDecode(Line, A);
        A733LineAppendText(Line, " activeRequired=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " deferredEvents=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " deferredErrors=");
        A733LineAppendHex32(Line, D);
        break;
    case 80U:
        A733LineAppendText(Line, " stageText=smallReadRespRegsLow");
        break;
    case 81U:
        A733LineAppendText(Line, " stageText=smallReadRespRegsHigh");
        break;
    case 82U:
        A733LineAppendText(Line, " stageText=commandDoneResponse cmdName=");
        A733LineAppendText(Line, A733SdCommandName(A));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, A);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, A);
        A733AppendR1Decode(Line, C);
        break;
    case 83U:
        A733LineAppendText(Line, " stageText=commandDoneResponseMeta responseName=");
        A733LineAppendText(Line, A733ResponseTypeName(A));
        A733AppendEventMaskDecode(Line, C);
        A733LineAppendText(Line, " deliveredEvents=");
        A733LineAppendHex32(Line, D);
        break;
    case 84U:
        A733LineAppendText(Line, " stageText=commandDoneResponseHigh");
        break;
    case 85U:
        A733LineAppendText(Line, " stageText=lastGetResponse");
        A733AppendResponseDecode(Line, A, B, D);
        break;
    case 86U:
        A733LineAppendText(Line, " stageText=lastGetResponseHigh");
        break;
    case 87U:
        A733LineAppendText(Line, " stageText=acmd51HardwareArgumentRewrite cmdName=");
        A733LineAppendText(Line, A733SdCommandName(A));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, A);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " sdportArgument=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " hardwareArgument=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " commandClassName=");
        A733LineAppendText(Line, A733CommandClassName(D));
        break;
    case 88U:
        RawState = (B >> 9U) & 0xFU;
        NormalizedState = (C >> 9U) & 0xFU;
        A733LineAppendText(Line, " stageText=cmd7R1BTranResponse cmdName=");
        A733LineAppendText(Line, A733SdCommandName(A));
        A733LineAppendText(Line, " cmdIndexDec=");
        A733LineAppendDecimal(Line, A);
        A733LineAppendText(Line, " cmdIndexHex=");
        A733LineAppendHex32(Line, A);
        A733LineAppendText(Line, " responseName=");
        A733LineAppendText(Line, A733ResponseTypeName(D));
        A733LineAppendText(Line, " rawResponse=");
        A733LineAppendHex32(Line, B);
        A733LineAppendText(Line, " rawState=");
        A733LineAppendDecimal(Line, RawState);
        A733LineAppendChar(Line, '(');
        A733LineAppendText(Line, A733R1CurrentStateName(RawState));
        A733LineAppendChar(Line, ')');
        A733LineAppendText(Line, " normalizedResponse=");
        A733LineAppendHex32(Line, C);
        A733LineAppendText(Line, " normalizedState=");
        A733LineAppendDecimal(Line, NormalizedState);
        A733LineAppendChar(Line, '(');
        A733LineAppendText(Line, A733R1CurrentStateName(NormalizedState));
        A733LineAppendChar(Line, ')');
        break;
    default:
        break;
    }
}

static VOID
A733AppendBusDecode(
    _Inout_ A733_LINE *Line,
    _In_reads_bytes_(6U * sizeof(ULONG)) const BYTE *Payload)
{
    ULONG RawType;
    ULONG Type;
    ULONG Value;
    ULONG Status;

    RawType = A733ReadPackedUlong(Payload);
    Type = RawType & 0x7FFFFFFFUL;
    Value = A733ReadPackedUlong(Payload + sizeof(ULONG));
    Status = A733ReadPackedUlong(Payload + (2U * sizeof(ULONG)));

    A733LineAppendText(Line, " busPhase=");
    A733LineAppendText(Line, ((RawType & 0x80000000UL) != 0) ?
                       "after" : "before");
    A733LineAppendText(Line, " busName=");
    A733LineAppendText(Line, A733BusOperationName(Type));
    A733LineAppendText(Line, " valueName=");
    A733LineAppendText(Line, A733BusValueName(Type, Value));
    A733LineAppendText(Line, " busValueName=");
    A733LineAppendText(Line, A733BusValueName(Type, Value));
    A733LineAppendText(Line, " busStatusName=");
    A733LineAppendText(Line, A733NtStatusName(Status));
}

static VOID WINAPI
A733EventRecordCallback(
    _In_ PEVENT_RECORD EventRecord)
{
    A733_LINE Line;
    const A733_EVENT_SCHEMA *Schema;
    const BYTE *Payload;
    ULONG ExpectedBytes;
    DWORD Index;

    A733ZeroBytes(&Line, sizeof(Line));
    A733_EventSequence += 1;
    A733LineAppendText(&Line, "A733_SMHC seq=");
    A733LineAppendDecimal(&Line, A733_EventSequence);
    A733LineAppendText(&Line, " time=");
    A733LineAppendHex64(
        &Line,
        (ULONGLONG)EventRecord->EventHeader.TimeStamp.QuadPart);
    A733LineAppendText(&Line, " cpu=");
    A733LineAppendDecimal(&Line, EventRecord->BufferContext.ProcessorNumber);
    A733LineAppendText(&Line, " pid=");
    A733LineAppendDecimal(&Line, EventRecord->EventHeader.ProcessId);
    A733LineAppendText(&Line, " tid=");
    A733LineAppendDecimal(&Line, EventRecord->EventHeader.ThreadId);

    Schema = A733FindSchema(EventRecord->EventHeader.EventDescriptor.Id);
    A733LineAppendText(&Line, " event=");
    if (Schema == NULL) {
        A733LineAppendText(&Line, "unknown id=");
        A733LineAppendDecimal(
            &Line,
            EventRecord->EventHeader.EventDescriptor.Id);
        A733LineAppendText(&Line, " size=");
        A733LineAppendDecimal(&Line, EventRecord->UserDataLength);
        A733CommitLogLine(&Line);
        return;
    }

    A733LineAppendText(&Line, Schema->Name);
    A733LineAppendText(&Line, " id=");
    A733LineAppendDecimal(&Line, Schema->Id);
    A733LineAppendText(&Line, " size=");
    A733LineAppendDecimal(&Line, EventRecord->UserDataLength);
    ExpectedBytes = (ULONG)Schema->UlongCount * (ULONG)sizeof(ULONG);
    if (((ULONG)EventRecord->UserDataLength != ExpectedBytes) ||
        (EventRecord->UserData == NULL)) {
        A733LineAppendText(&Line, " payload=invalid expected=");
        A733LineAppendDecimal(&Line, ExpectedBytes);
        A733CommitLogLine(&Line);
        return;
    }

    Payload = (const BYTE *)EventRecord->UserData;
    for (Index = 0; Index < Schema->UlongCount; Index += 1) {
        A733LineAppendChar(&Line, ' ');
        A733LineAppendText(&Line, Schema->FieldNames[Index]);
        A733LineAppendChar(&Line, '=');
        A733LineAppendHex32(
            &Line,
            A733ReadPackedUlong(Payload + (Index * sizeof(ULONG))));
    }
    if (Schema->Id == 2) {
        A733AppendBusDecode(&Line, Payload);
    }
    if (Schema->Id == 1) {
        A733AppendLifecycleDecode(&Line, Payload);
    }
    if (Schema->Id == 3) {
        A733AppendIssueDecode(&Line, Payload);
    }
    if (Schema->Id == 6) {
        A733AppendTransferDecode(&Line, Payload);
    }
    if (Schema->Id == 9) {
        A733AppendResponseDecode(
            &Line,
            A733ReadPackedUlong(Payload),
            A733ReadPackedUlong(Payload + sizeof(ULONG)),
            A733ReadPackedUlong(Payload + (2U * sizeof(ULONG))));
    }
    if (Schema->Id == 10) {
        ULONG Reason;

        Reason = A733ReadPackedUlong(Payload + (4U * sizeof(ULONG)));
        if (Reason == 1U) {
            A733LineAppendText(
                &Line,
                " reasonText=ack-observed-transfer-completion"
                " observe=terminal-RINT-cleared-before-request-complete");
        } else if (Reason == 2U) {
            A733LineAppendText(
                &Line,
                " reasonText=restore-software-interrupt-mask-after-polling"
                " observe=IMASK-matches-enabled-policy");
        } else if (Reason == 3U) {
            A733LineAppendText(
                &Line,
                " reasonText=restore-global-interrupt-gate-after-polling"
                " observe=GCTRL-interrupt-enable-matches-IMASK");
        } else if (Reason == 4U) {
            A733LineAppendText(
                &Line,
                " reasonText=mask-level-data-request-after-event-delivery"
                " observe=no-repeat-before-StartTransfer-drains-FIFO");
        } else if (Reason == 5U) {
            A733LineAppendText(
                &Line,
                " reasonText=quiesce-software-interrupt-mask"
                " observe=request-interrupts-masked-before-retire");
        } else if (Reason == 6U) {
            A733LineAppendText(
                &Line,
                " reasonText=quiesce-latched-rint-ack"
                " observe=terminal-and-stale-RINT-cleared-before-retire");
        } else if (Reason == 7U) {
            A733LineAppendText(
                &Line,
                " reasonText=quiesce-global-interrupt-gate"
                " observe=GCTRL-interrupt-enable-matches-quiesced-IMASK");
        } else if (Reason == 8U) {
            A733LineAppendText(
                &Line,
                " reasonText=restore-controller-common-state"
                " observe=AHB-FIFO-access-and-common-registers-restored");
        } else if (Reason == 9U) {
            ULONG ResetType;

            ResetType = A733ReadPackedUlong(Payload + (5U * sizeof(ULONG)));
            A733LineAppendText(
                &Line,
                " reasonText=narrow-host-reset-request resetTypeName=");
            A733LineAppendText(&Line, A733ResetTypeName(ResetType));
            if (ResetType == 3U) {
                A733LineAppendText(
                    &Line,
                    " observe=DAT-reset-clears-FIFO-DMA-without-full-card-reinit");
            } else if (ResetType == 2U) {
                A733LineAppendText(
                    &Line,
                    " observe=CMD-reset-soft-resets-controller-command-path");
            } else {
                A733LineAppendText(
                    &Line,
                    " observe=narrow-reset-requested");
            }
        } else if (Reason == 10U) {
            A733LineAppendText(
                &Line,
                " reasonText=issue-command-register-write"
                " observe=SMHC_CMD-programmed-for-current-SDPORT-command");
        } else if (Reason == 11U) {
            A733LineAppendText(
                &Line,
                " reasonText=clear-all-known-latched-rint"
                " observe=request-SDIO-card-change-RINT-cleared-after-reset");
        } else {
            A733LineAppendText(
                &Line,
                " reasonText=unknown observe=unspecified");
        }
        A733LineAppendText(
            &Line,
            " persistenceText=nonpersistent-until-reset-or-power-cycle");
    }
    A733CommitLogLine(&Line);
}

static VOID
A733InitializeTraceProperties(
    _Out_ A733_TRACE_PROPERTIES_BUFFER *Buffer)
{
    DWORD Index;

    A733ZeroBytes(Buffer, sizeof(*Buffer));
    Buffer->Properties.Wnode.BufferSize = (ULONG)sizeof(*Buffer);
    Buffer->Properties.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    Buffer->Properties.Wnode.ClientContext = 1;
    Buffer->Properties.BufferSize = A733_TRACE_BUFFER_SIZE_KB;
    Buffer->Properties.MinimumBuffers = A733_TRACE_MINIMUM_BUFFERS;
    Buffer->Properties.MaximumBuffers = A733_TRACE_MAXIMUM_BUFFERS;
    Buffer->Properties.LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    Buffer->Properties.FlushTimer = 1;
    Buffer->Properties.LoggerNameOffset =
        (ULONG)FIELD_OFFSET(A733_TRACE_PROPERTIES_BUFFER, LoggerName);

    Index = 0;
    while ((A733_SESSION_NAME[Index] != L'\0') &&
           (Index < (A733_SESSION_NAME_CAPACITY - 1U))) {
        Buffer->LoggerName[Index] = A733_SESSION_NAME[Index];
        Index += 1;
    }
    Buffer->LoggerName[Index] = L'\0';
}

static ULONG
A733StopSession(
    _Out_opt_ A733_TRACE_PROPERTIES_BUFFER *ReturnedProperties)
{
    A733_TRACE_PROPERTIES_BUFFER PropertiesBuffer;
    ULONG Status;

    A733InitializeTraceProperties(&PropertiesBuffer);
    Status = ControlTraceW(
        (CONTROLTRACE_ID)0,
        A733_SESSION_NAME,
        &PropertiesBuffer.Properties,
        EVENT_TRACE_CONTROL_STOP);
    if (ReturnedProperties != NULL) {
        *ReturnedProperties = PropertiesBuffer;
    }
    return Status;
}

static BOOL
A733WideEquals(
    _In_z_ const WCHAR *Left,
    _In_z_ const WCHAR *Right)
{
    while ((*Left != L'\0') && (*Right != L'\0')) {
        if (*Left != *Right) {
            return FALSE;
        }
        Left += 1;
        Right += 1;
    }
    return (*Left == L'\0') && (*Right == L'\0');
}

static BOOL
A733CreateMarkerFile(
    _In_z_ const WCHAR *Path,
    _Inout_ A733_LINE *Line)
{
    HANDLE File;
    BOOL Success;
    DWORD Error;

    File = CreateFileW(
        Path,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (File == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    Success = A733WriteLineToHandle(File, Line, TRUE);
    Error = Success ? ERROR_SUCCESS : GetLastError();
    if (!CloseHandle(File) && Success) {
        Error = GetLastError();
        Success = FALSE;
    }
    if (!Success) {
        SetLastError(Error);
    }
    return Success;
}

static BOOL
A733CreateReadyFile(
    _In_z_ const WCHAR *ReadyPath)
{
    A733_LINE Line;

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733SmhcTrace ready");
    return A733CreateMarkerFile(ReadyPath, &Line);
}

static BOOL
A733CreateDoneFile(
    _In_z_ const WCHAR *DonePath,
    _In_ DWORD Status)
{
    A733_LINE Line;

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733SmhcTrace done status=");
    A733LineAppendHex32(&Line, Status);
    return A733CreateMarkerFile(DonePath, &Line);
}

static PWSTR
A733AllocateDonePath(
    _In_z_ const WCHAR *ReadyPath)
{
    static const WCHAR Suffix[] = L".done";
    SIZE_T Length;
    SIZE_T Index;
    PWSTR DonePath;

    Length = 0;
    while (ReadyPath[Length] != L'\0') {
        Length += 1;
        if (Length > A733_MAX_PATH_ARGUMENT_CHARS) {
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
            return NULL;
        }
    }
    DonePath = (PWSTR)HeapAlloc(
        GetProcessHeap(),
        0,
        (Length + ARRAYSIZE(Suffix)) * sizeof(WCHAR));
    if (DonePath == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    for (Index = 0; Index < Length; Index += 1) {
        DonePath[Index] = ReadyPath[Index];
    }
    for (Index = 0; Index < ARRAYSIZE(Suffix); Index += 1) {
        DonePath[Length + Index] = Suffix[Index];
    }
    return DonePath;
}

static BOOL
A733DeleteStaleMarker(
    _In_z_ const WCHAR *Path)
{
    DWORD Error;

    if (DeleteFileW(Path)) {
        return TRUE;
    }
    Error = GetLastError();
    if (Error == ERROR_FILE_NOT_FOUND) {
        return TRUE;
    }
    SetLastError(Error);
    return FALSE;
}

static VOID
A733WriteCaptureState(
    _In_z_ const CHAR *State,
    _In_ DWORD Status)
{
    A733_LINE Line;

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_TRACE state=");
    A733LineAppendText(&Line, State);
    A733LineAppendText(&Line, " session=A733SmhcTrace status=");
    A733LineAppendHex32(&Line, Status);
    A733CommitLogLine(&Line);
}

static VOID
A733WriteTraceLoss(
    _In_ ULONG EventsLost,
    _In_ ULONG BuffersLost)
{
    A733_LINE Line;

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_TRACE loss events=");
    A733LineAppendDecimal(&Line, EventsLost);
    A733LineAppendText(&Line, " buffers=");
    A733LineAppendDecimal(&Line, BuffersLost);
    A733CommitLogLine(&Line);
}

static DWORD
A733Capture(
    _In_z_ const WCHAR *LogPath,
    _In_z_ const WCHAR *ReadyPath,
    _In_ BOOL LiveConsole,
    _In_ BOOL LiveInterestingOnly)
{
    A733_TRACE_PROPERTIES_BUFFER PropertiesBuffer;
    ENABLE_TRACE_PARAMETERS EnableParameters;
    EVENT_TRACE_LOGFILEW LogFile;
    PROCESSTRACE_HANDLE ConsumerHandle;
    CONTROLTRACE_ID SessionHandle;
    ULONG Status;
    DWORD Error;
    DWORD CompletionError;
    BOOL SessionStarted;
    BOOL ConsumerOpened;
    PWSTR DonePath;

    DonePath = A733AllocateDonePath(ReadyPath);
    if (DonePath == NULL) {
        Error = GetLastError();
        A733ReportError("allocate-done-path", Error);
        return Error;
    }

    A733_LogHandle = INVALID_HANDLE_VALUE;
    A733_LogError = ERROR_SUCCESS;
    A733_LiveConsole = LiveConsole;
    A733_LiveInterestingOnly = LiveInterestingOnly;
    A733_EventSequence = 0;
    A733_LogLinesSinceFlush = 0;
    SessionStarted = FALSE;
    ConsumerOpened = FALSE;
    SessionHandle = (CONTROLTRACE_ID)0;
    ConsumerHandle = INVALID_PROCESSTRACE_HANDLE;
    if (A733WideEquals(LogPath, ReadyPath)) {
        A733ReportError("capture-paths-identical", ERROR_INVALID_PARAMETER);
        Error = ERROR_INVALID_PARAMETER;
        goto Exit;
    }
    if (A733WideEquals(LogPath, DonePath)) {
        A733ReportError("log-path-is-done-path", ERROR_INVALID_PARAMETER);
        Error = ERROR_INVALID_PARAMETER;
        goto Exit;
    }
    if (!A733DeleteStaleMarker(ReadyPath)) {
        Error = GetLastError();
        A733ReportError("delete-stale-ready", Error);
        goto Exit;
    }
    if (!A733DeleteStaleMarker(DonePath)) {
        Error = GetLastError();
        A733ReportError("delete-stale-done", Error);
        goto Exit;
    }

    A733_LogHandle = CreateFileW(
        LogPath,
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
        NULL);
    if (A733_LogHandle == INVALID_HANDLE_VALUE) {
        Error = GetLastError();
        A733ReportError("create-log", Error);
        goto Exit;
    }

    A733InitializeTraceProperties(&PropertiesBuffer);
    Status = StartTraceW(
        &SessionHandle,
        A733_SESSION_NAME,
        &PropertiesBuffer.Properties);
    if (Status != ERROR_SUCCESS) {
        A733WriteCaptureState("start-failed", Status);
        A733ReportError("StartTraceW", Status);
        Error = Status;
        goto Exit;
    }
    SessionStarted = TRUE;

    A733ZeroBytes(&EnableParameters, sizeof(EnableParameters));
    EnableParameters.Version = ENABLE_TRACE_PARAMETERS_VERSION_2;
    Status = EnableTraceEx2(
        SessionHandle,
        &A733_PROVIDER_GUID,
        EVENT_CONTROL_CODE_ENABLE_PROVIDER,
        TRACE_LEVEL_VERBOSE,
        0ULL,
        0ULL,
        0,
        &EnableParameters);
    if (Status != ERROR_SUCCESS) {
        A733WriteCaptureState("enable-failed", Status);
        A733ReportError("EnableTraceEx2", Status);
        Error = Status;
        goto Exit;
    }

    A733ZeroBytes(&LogFile, sizeof(LogFile));
    LogFile.LoggerName = (LPWSTR)A733_SESSION_NAME;
    LogFile.ProcessTraceMode =
        PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    LogFile.EventRecordCallback = A733EventRecordCallback;
    ConsumerHandle = OpenTraceW(&LogFile);
    if (ConsumerHandle == INVALID_PROCESSTRACE_HANDLE) {
        Error = GetLastError();
        A733WriteCaptureState("open-consumer-failed", Error);
        A733ReportError("OpenTraceW", Error);
        goto Exit;
    }
    ConsumerOpened = TRUE;

    A733WriteCaptureState("ready", ERROR_SUCCESS);
    if ((A733_LogError != ERROR_SUCCESS) || !A733FlushLog()) {
        Error = A733_LogError;
        if (Error == ERROR_SUCCESS) {
            Error = GetLastError();
        }
        A733ReportError("flush-ready-log", Error);
        goto Exit;
    }
    if (!A733CreateReadyFile(ReadyPath)) {
        Error = GetLastError();
        A733WriteCaptureState("ready-file-failed", Error);
        A733ReportError("create-ready", Error);
        goto Exit;
    }

    Status = ProcessTrace(&ConsumerHandle, 1, NULL, NULL);
    Error = Status;
    if (Status == ERROR_SUCCESS) {
        if ((LogFile.LogfileHeader.EventsLost != 0) ||
            (LogFile.LogfileHeader.BuffersLost != 0)) {
            A733WriteTraceLoss(LogFile.LogfileHeader.EventsLost,
                               LogFile.LogfileHeader.BuffersLost);
            Error = ERROR_MORE_DATA;
            A733WriteCaptureState("loss-detected", Error);
        } else {
            A733WriteCaptureState("stopped", Status);
        }
    } else {
        A733WriteCaptureState("process-failed", Status);
        A733ReportError("ProcessTrace", Status);
    }

Exit:
    (VOID)DeleteFileW(ReadyPath);
    if (ConsumerOpened) {
        Status = CloseTrace(ConsumerHandle);
        if ((Status != ERROR_SUCCESS) && (Error == ERROR_SUCCESS)) {
            Error = Status;
            A733ReportError("CloseTrace", Status);
        }
    }
    if (SessionStarted && (Error != ERROR_SUCCESS)) {
        (VOID)A733StopSession(NULL);
    }
    if (A733_LogError != ERROR_SUCCESS) {
        A733ReportError("log-write", A733_LogError);
        if (Error == ERROR_SUCCESS) {
            Error = A733_LogError;
        }
    }
    if ((A733_LogHandle != NULL) &&
        (A733_LogHandle != INVALID_HANDLE_VALUE)) {
        if (!A733FlushLog()) {
            CompletionError = A733_LogError;
            if (CompletionError == ERROR_SUCCESS) {
                CompletionError = GetLastError();
            }
            A733ReportError("final-log-flush", CompletionError);
            if (Error == ERROR_SUCCESS) {
                Error = CompletionError;
            }
        }
        if (!CloseHandle(A733_LogHandle)) {
            CompletionError = GetLastError();
            A733ReportError("close-log", CompletionError);
            if (Error == ERROR_SUCCESS) {
                Error = CompletionError;
            }
        }
    }
    A733_LogHandle = INVALID_HANDLE_VALUE;
    if (!A733CreateDoneFile(DonePath, Error)) {
        CompletionError = GetLastError();
        A733ReportError("create-done", CompletionError);
        if (Error == ERROR_SUCCESS) {
            Error = CompletionError;
        }
    }
    (VOID)HeapFree(GetProcessHeap(), 0, DonePath);
    A733_LiveConsole = FALSE;
    A733_LiveInterestingOnly = FALSE;
    return Error;
}

static DWORD
A733Stop(VOID)
{
    A733_TRACE_PROPERTIES_BUFFER PropertiesBuffer;
    A733_LINE Line;
    HANDLE Output;
    ULONG Status;

    Status = A733StopSession(&PropertiesBuffer);
    if (Status == ERROR_WMI_INSTANCE_NOT_FOUND) {
        A733WriteConsoleText(
            STD_OUTPUT_HANDLE,
            "A733_SMHC_TRACE_STOP status=already-stopped\r\n");
        return ERROR_SUCCESS;
    }
    if (Status == ERROR_SUCCESS) {
        A733ZeroBytes(&Line, sizeof(Line));
        A733LineAppendText(&Line, "A733_SMHC_TRACE_STOP status=stopped events_lost=");
        A733LineAppendDecimal(&Line, PropertiesBuffer.Properties.EventsLost);
        A733LineAppendText(&Line, " realtime_buffers_lost=");
        A733LineAppendDecimal(
            &Line,
            PropertiesBuffer.Properties.RealTimeBuffersLost);
        A733LineAppendText(&Line, " log_buffers_lost=");
        A733LineAppendDecimal(
            &Line,
            PropertiesBuffer.Properties.LogBuffersLost);
        Output = GetStdHandle(STD_OUTPUT_HANDLE);
        if ((Output != NULL) && (Output != INVALID_HANDLE_VALUE)) {
            (VOID)A733WriteLineToHandle(Output, &Line, FALSE);
        }
        if ((PropertiesBuffer.Properties.EventsLost != 0) ||
            (PropertiesBuffer.Properties.RealTimeBuffersLost != 0) ||
            (PropertiesBuffer.Properties.LogBuffersLost != 0)) {
            A733ReportError("ControlTraceW-loss", ERROR_MORE_DATA);
            return ERROR_MORE_DATA;
        }
        return ERROR_SUCCESS;
    }
    A733ReportError("ControlTraceW-stop", Status);
    return Status;
}

static BOOL
A733ParsePositiveUlong(
    _In_z_ const WCHAR *Text,
    _Out_ ULONG *Value)
{
    ULONG Result;
    ULONG Digit;

    if (*Text == L'\0') {
        return FALSE;
    }
    Result = 0;
    while (*Text != L'\0') {
        if ((*Text < L'0') || (*Text > L'9')) {
            return FALSE;
        }
        Digit = (ULONG)(*Text - L'0');
        if (Result > ((A733_MAX_DUMP_LINES - Digit) / 10U)) {
            return FALSE;
        }
        Result = (Result * 10U) + Digit;
        Text += 1;
    }
    if (Result == 0) {
        return FALSE;
    }
    *Value = Result;
    return TRUE;
}

static BOOL
A733FindTailStart(
    _In_ HANDLE File,
    _In_ ULONGLONG FileSize,
    _In_ ULONG MaxLines,
    _Out_ ULONGLONG *StartOffset)
{
    ULONGLONG Cursor;
    ULONGLONG Offset;
    ULONGLONG AbsoluteOffset;
    LARGE_INTEGER SeekOffset;
    DWORD Requested;
    DWORD Read;
    DWORD Index;
    ULONG Newlines;

    Cursor = FileSize;
    Newlines = 0;
    *StartOffset = 0;
    while (Cursor != 0) {
        Requested = (Cursor > A733_DUMP_CHUNK_BYTES) ?
            A733_DUMP_CHUNK_BYTES : (DWORD)Cursor;
        Offset = Cursor - Requested;
        SeekOffset.QuadPart = (LONGLONG)Offset;
        if (!SetFilePointerEx(File, SeekOffset, NULL, FILE_BEGIN)) {
            return FALSE;
        }
        Read = 0;
        if (!ReadFile(File, A733_DumpBuffer, Requested, &Read, NULL)) {
            return FALSE;
        }
        if (Read != Requested) {
            SetLastError(ERROR_HANDLE_EOF);
            return FALSE;
        }
        Index = Read;
        while (Index != 0) {
            Index -= 1;
            AbsoluteOffset = Offset + Index;
            if (A733_DumpBuffer[Index] == (BYTE)'\n') {
                if (AbsoluteOffset == (FileSize - 1ULL)) {
                    continue;
                }
                Newlines += 1;
                if (Newlines == MaxLines) {
                    *StartOffset = AbsoluteOffset + 1ULL;
                    return TRUE;
                }
            }
        }
        Cursor = Offset;
    }
    return TRUE;
}

static DWORD
A733WritePacedDumpChunk(
    _In_ HANDLE Output,
    _In_ DWORD Length,
    _Inout_ BOOL *AtLineStart,
    _Inout_ BOOL *CompletedLine)
{
    DWORD Index;
    DWORD SegmentStart;

    SegmentStart = 0;
    for (Index = 0; Index < Length; Index += 1) {
        if (A733_DumpBuffer[Index] == (BYTE)'\n') {
            if (*AtLineStart && *CompletedLine) {
                Sleep(A733_DUMP_LINE_DELAY_MS);
            }
            if (!A733WriteAll(
                    Output,
                    (const CHAR *)&A733_DumpBuffer[SegmentStart],
                    (Index - SegmentStart) + 1U)) {
                return GetLastError();
            }
            *AtLineStart = TRUE;
            *CompletedLine = TRUE;
            SegmentStart = Index + 1U;
        }
    }
    if (SegmentStart < Length) {
        if (*AtLineStart && *CompletedLine) {
            Sleep(A733_DUMP_LINE_DELAY_MS);
        }
        if (!A733WriteAll(
                Output,
                (const CHAR *)&A733_DumpBuffer[SegmentStart],
                Length - SegmentStart)) {
            return GetLastError();
        }
        *AtLineStart = FALSE;
    }
    return ERROR_SUCCESS;
}

static DWORD
A733Dump(
    _In_z_ const WCHAR *LogPath,
    _In_ ULONG MaxLines)
{
    HANDLE File;
    HANDLE Output;
    LARGE_INTEGER Size;
    LARGE_INTEGER SeekOffset;
    ULONGLONG StartOffset;
    DWORD Read;
    DWORD Error;
    BOOL AtLineStart;
    BOOL CompletedLine;

    File = CreateFileW(
        LogPath,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (File == INVALID_HANDLE_VALUE) {
        Error = GetLastError();
        A733ReportError("dump-open", Error);
        return Error;
    }
    if (!GetFileSizeEx(File, &Size)) {
        Error = GetLastError();
        A733ReportError("dump-size", Error);
        (VOID)CloseHandle(File);
        return Error;
    }
    if (!A733FindTailStart(File, (ULONGLONG)Size.QuadPart, MaxLines,
                           &StartOffset)) {
        Error = GetLastError();
        A733ReportError("dump-find-tail", Error);
        (VOID)CloseHandle(File);
        return Error;
    }

    SeekOffset.QuadPart = (LONGLONG)StartOffset;
    if (!SetFilePointerEx(File, SeekOffset, NULL, FILE_BEGIN)) {
        Error = GetLastError();
        A733ReportError("dump-seek", Error);
        (VOID)CloseHandle(File);
        return Error;
    }
    Output = GetStdHandle(STD_OUTPUT_HANDLE);
    if ((Output == NULL) || (Output == INVALID_HANDLE_VALUE)) {
        Error = ERROR_INVALID_HANDLE;
        A733ReportError("dump-stdout", Error);
        (VOID)CloseHandle(File);
        return Error;
    }
    AtLineStart = TRUE;
    CompletedLine = FALSE;
    for (;;) {
        Read = 0;
        if (!ReadFile(File,
                      A733_DumpBuffer,
                      A733_DUMP_CHUNK_BYTES,
                      &Read,
                      NULL)) {
            Error = GetLastError();
            A733ReportError("dump-read", Error);
            (VOID)CloseHandle(File);
            return Error;
        }
        if (Read == 0) {
            break;
        }
        Error = A733WritePacedDumpChunk(
            Output,
            Read,
            &AtLineStart,
            &CompletedLine);
        if (Error != ERROR_SUCCESS) {
            A733ReportError("dump-write", Error);
            (VOID)CloseHandle(File);
            return Error;
        }
    }
    (VOID)CloseHandle(File);
    return ERROR_SUCCESS;
}

static VOID
A733LineAppendBoundedBytes(
    _Inout_ A733_LINE *Line,
    _In_reads_bytes_(Length) const CHAR *Data,
    _In_ DWORD Length)
{
    DWORD Index;

    for (Index = 0;
         (Index < Length) && (Line->Length < (A733_LINE_CAPACITY - 2U));
         Index += 1) {
        A733LineAppendChar(Line, Data[Index]);
    }
}

static VOID
A733SummaryRememberLine(
    _Inout_ A733_SUMMARY_ENTRY *Entry,
    _In_ const A733_LINE *Line)
{
    DWORD CopyLength;

    CopyLength = Line->Length;
    if (CopyLength > (A733_LINE_CAPACITY - 2U)) {
        CopyLength = A733_LINE_CAPACITY - 2U;
    }
    if (!Entry->HasFirst) {
        A733ZeroBytes(&Entry->FirstLine, sizeof(Entry->FirstLine));
        if (CopyLength != 0) {
            A733CopyBytes(Entry->FirstLine.Data, Line->Data, CopyLength);
        }
        Entry->FirstLine.Length = CopyLength;
        Entry->FirstLine.Truncated = FALSE;
        Entry->HasFirst = TRUE;
    }
    A733ZeroBytes(&Entry->LastLine, sizeof(Entry->LastLine));
    if (CopyLength != 0) {
        A733CopyBytes(Entry->LastLine.Data, Line->Data, CopyLength);
    }
    Entry->LastLine.Length = CopyLength;
    Entry->LastLine.Truncated = FALSE;
    Entry->HasLast = TRUE;
}

static VOID
A733RecentRememberLine(
    _Inout_updates_(A733_SUMMARY_RECENT_LINES) A733_LINE *RecentLines,
    _Inout_ DWORD *RecentCount,
    _Inout_ DWORD *RecentNext,
    _In_ const A733_LINE *Line)
{
    DWORD CopyLength;
    A733_LINE *Destination;

    Destination = &RecentLines[*RecentNext];
    A733ZeroBytes(Destination, sizeof(*Destination));
    CopyLength = Line->Length;
    if (CopyLength > (A733_LINE_CAPACITY - 2U)) {
        CopyLength = A733_LINE_CAPACITY - 2U;
    }
    if (CopyLength != 0) {
        A733CopyBytes(Destination->Data, Line->Data, CopyLength);
    }
    Destination->Length = CopyLength;
    Destination->Truncated = FALSE;

    *RecentNext += 1U;
    if (*RecentNext == A733_SUMMARY_RECENT_LINES) {
        *RecentNext = 0;
    }
    if (*RecentCount < A733_SUMMARY_RECENT_LINES) {
        *RecentCount += 1U;
    }
}

static VOID
A733SummaryIncrementByName(
    _Inout_updates_(EntryCount) A733_SUMMARY_ENTRY *Entries,
    _In_ DWORD EntryCount,
    _In_z_ const CHAR *Name,
    _In_ const A733_LINE *Line)
{
    DWORD Index;

    for (Index = ARRAYSIZE(A733_EVENT_SCHEMAS);
         Index < EntryCount;
         Index += 1) {
        if (A733StringEquals(Entries[Index].Name, Name)) {
            Entries[Index].Count += 1ULL;
            A733SummaryRememberLine(&Entries[Index], Line);
            return;
        }
    }
}

static VOID
A733SummaryObserveCommandCombo(
    _Inout_updates_(EntryCount) A733_SUMMARY_ENTRY *Entries,
    _In_ DWORD EntryCount,
    _In_ const A733_LINE *Line,
    _In_z_ const CHAR *CommandName,
    _In_z_ const CHAR *IssueCombo,
    _In_z_ const CHAR *TransferCombo,
    _In_z_ const CHAR *ResponseCombo)
{
    if (!A733LineContains(Line, CommandName)) {
        return;
    }
    if ((IssueCombo != NULL) &&
        A733LineContains(Line, " event=issue ")) {
        A733SummaryIncrementByName(Entries, EntryCount, IssueCombo, Line);
    }
    if ((TransferCombo != NULL) &&
        A733LineContains(Line, " event=transfer ")) {
        A733SummaryIncrementByName(Entries, EntryCount, TransferCombo, Line);
    }
    if ((ResponseCombo != NULL) &&
        A733LineContains(Line, " event=response ")) {
        A733SummaryIncrementByName(Entries, EntryCount, ResponseCombo, Line);
    }
}

static VOID
A733SummaryObserveLine(
    _Inout_updates_(EntryCount) A733_SUMMARY_ENTRY *Entries,
    _In_ DWORD EntryCount,
    _In_ const A733_LINE *Line)
{
    DWORD Index;

    for (Index = 0; Index < ARRAYSIZE(A733_EVENT_SCHEMAS); Index += 1) {
        if (A733LineContainsEventName(Line, Entries[Index].Name)) {
            Entries[Index].Count += 1ULL;
            A733SummaryRememberLine(&Entries[Index], Line);
            break;
        }
    }

    for (Index = ARRAYSIZE(A733_EVENT_SCHEMAS);
         Index < EntryCount;
         Index += 1) {
        if (A733LineContains(Line, Entries[Index].Name)) {
            Entries[Index].Count += 1ULL;
            A733SummaryRememberLine(&Entries[Index], Line);
        }
    }

    if (A733LineContains(Line, "status=0xC00000B5")) {
        if (A733LineContains(Line, " event=transfer ")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:transferIoTimeout",
                Line);
        }
        if (A733LineContains(Line, " event=issue ")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:issueIoTimeout",
                Line);
        }
        if (A733LineContains(Line, " event=dpc ")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:dpcIoTimeout",
                Line);
        }
        if (A733LineContains(Line, " event=lifecycle ")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:lifecycleIoTimeout",
                Line);
        }
    }
    if (A733LineContains(Line, "stageText=smallReadPostCompletion") &&
        A733LineContains(Line, "requestStatus=0xC00000B5")) {
        A733SummaryIncrementByName(
            Entries,
            EntryCount,
            "combo:smallReadPostIoTimeout",
            Line);
    }
    if (A733LineContains(Line, "stageText=smallReadRequest") &&
        A733LineContains(Line, "cmdName=ACMD51_SEND_SCR")) {
        A733SummaryIncrementByName(
            Entries,
            EntryCount,
            "combo:smallReadReqCmd51",
            Line);
    }
    if (A733LineContains(Line, "stageText=commandDoneResponse") &&
        A733LineContains(Line, "cmdName=CMD9_SEND_CSD")) {
        A733SummaryIncrementByName(
            Entries,
            EntryCount,
            "combo:cmdDoneResp9",
            Line);
    }
    if (A733LineContains(Line, "stageText=commandDoneResponse") &&
        A733LineContains(Line, "cmdName=ACMD42_SET_CLR_CARD_DETECT")) {
        A733SummaryIncrementByName(
            Entries,
            EntryCount,
            "combo:cmdDoneResp42",
            Line);
    }
    if (A733LineContains(Line, "stageText=commandDoneResponseMeta") &&
        A733LineContains(Line, "responseName=SdResponseTypeR2")) {
        A733SummaryIncrementByName(
            Entries,
            EntryCount,
            "combo:cmdDoneMetaR2",
            Line);
    }
    if (A733LineContains(Line, "stageText=lastGetResponse") &&
        A733LineContains(Line, "cmdName=CMD9_SEND_CSD")) {
        A733SummaryIncrementByName(
            Entries,
            EntryCount,
            "combo:lastGetResp9",
            Line);
    }
    if (A733LineContains(Line, "stageText=lastGetResponse") &&
        A733LineContains(Line, "cmdName=ACMD42_SET_CLR_CARD_DETECT")) {
        A733SummaryIncrementByName(
            Entries,
            EntryCount,
            "combo:lastGetResp42",
            Line);
    }
    if (A733LineContains(Line, "stageText=cmd9R2Raw")) {
        A733SummaryIncrementByName(
            Entries,
            EntryCount,
            "combo:cmd9R2Raw",
            Line);
    }
    if (A733LineContains(Line, "stageText=cmd9R2Out")) {
        A733SummaryIncrementByName(
            Entries,
            EntryCount,
            "combo:cmd9R2Out",
            Line);
    }
    if (A733LineContains(Line, "stageText=cmd2R2Raw")) {
        A733SummaryIncrementByName(
            Entries,
            EntryCount,
            "combo:cmd2R2Raw",
            Line);
    }
    if (A733LineContains(Line, "stageText=cmd2R2Out")) {
        A733SummaryIncrementByName(
            Entries,
            EntryCount,
            "combo:cmd2R2Out",
            Line);
    }
    if (A733LineContains(Line, "stageText=cmd6R1ForceTran")) {
        A733SummaryIncrementByName(
            Entries,
            EntryCount,
            "combo:cmd6R1ForceTran",
            Line);
    }
    if (A733LineContains(Line, "stageText=cmd6IgnoredCommandErrors")) {
        A733SummaryIncrementByName(
            Entries,
            EntryCount,
            "combo:cmd6IgnoredCommandErrors",
            Line);
    }
    if (A733LineContains(Line, "stageText=syntheticAcmd51ScrWords")) {
        A733SummaryIncrementByName(
            Entries,
            EntryCount,
            "combo:syntheticScrWords",
            Line);
    }
    if (A733LineContains(Line, "stageText=acmdR1AppCmdClear")) {
        A733SummaryIncrementByName(
            Entries,
            EntryCount,
            "combo:acmdR1AppCmdClear",
            Line);
    }
    if (A733LineContains(Line, " event=bus ")) {
        if (A733LineContains(Line, "status=0x00000000")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:busStatusSuccess",
                Line);
        }
        if (A733LineContains(Line, "status=0xC00000BB")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:busStatusNotSupported",
                Line);
        }
        if (A733LineContains(Line, "status=0xC000000D")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:busStatusInvalidParameter",
                Line);
        }
        if (A733LineContains(Line, "status=0xC00000B5")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:busStatusIoTimeout",
                Line);
        }
        if (A733LineContains(Line, "status=0x00000103")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:busStatusPending",
                Line);
        }
    }
    if (A733LineContains(Line, "stageText=lastCommandDetailEvents") &&
        A733LineContains(Line, "requestStatus=0xC00000B5")) {
        A733SummaryIncrementByName(
            Entries,
            EntryCount,
            "combo:lastCommandEventsIoTimeout",
            Line);
    }
    if (A733LineContains(Line, "stageText=lastCommandForReset")) {
        if (A733LineContains(Line, "resetTypeName=SdResetTypeDat")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:lastResetDat",
                Line);
            if (A733LineContains(Line, "cmdName=CMD7_SELECT_CARD")) {
                A733SummaryIncrementByName(
                    Entries,
                    EntryCount,
                    "combo:lastResetDatCmd7",
                    Line);
            }
        }
        if (A733LineContains(Line, "resetTypeName=SdResetTypeCmd")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:lastResetCmd",
                Line);
        }
        if (A733LineContains(Line, "resetTypeName=SdResetTypeAll")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:lastResetAll",
                Line);
        }
    }
    A733SummaryObserveCommandCombo(
        Entries,
        EntryCount,
        Line,
        "cmdName=CMD0_GO_IDLE_STATE",
        "combo:issueCmd0",
        NULL,
        NULL);
    A733SummaryObserveCommandCombo(
        Entries,
        EntryCount,
        Line,
        "cmdName=CMD2_ALL_SEND_CID",
        "combo:issueCmd2",
        NULL,
        "combo:responseCmd2");
    A733SummaryObserveCommandCombo(
        Entries,
        EntryCount,
        Line,
        "cmdName=CMD3_SEND_RELATIVE_ADDR",
        "combo:issueCmd3",
        NULL,
        "combo:responseCmd3");
    A733SummaryObserveCommandCombo(
        Entries,
        EntryCount,
        Line,
        "cmdName=CMD6_SWITCH_OR_ACMD6_SET_BUS_WIDTH",
        "combo:issueCmd6",
        "combo:transferCmd6",
        "combo:responseCmd6");
    if (A733LineContains(Line, "cmdName=CMD6_SWITCH_OR_ACMD6_SET_BUS_WIDTH") &&
        A733LineContains(Line, " event=issue ")) {
        if (A733LineContains(Line, " requestType=0x00000001")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:issueCmd6NoTransfer",
                Line);
        }
        if (A733LineContains(Line, " requestType=0x00000002")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:issueCmd6Transfer",
                Line);
        }
    }
    if (A733LineContains(Line,
                         "cmdName=CMD6_SWITCH_OR_ACMD6_SET_BUS_WIDTH")) {
        if (A733LineContains(Line,
                             "stageText=writeSuccessAlternateCompletion")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd6DeferredQueued",
                Line);
        }
        if (A733LineContains(Line, "stageText=deferredCompleteEnter")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd6DeferredEnter",
                Line);
        }
        if (A733LineContains(Line, "stageText=deferredCompleteExit")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd6DeferredExit",
                Line);
        }
        if (A733LineContains(Line, "stageText=deferredCompleteError")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd6DeferredError",
                Line);
        }
        if (A733LineContains(Line, "stageText=cmd6SyntheticStatus")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd6SyntheticStatus",
                Line);
        }
        if (A733LineContains(Line, "stageText=cmd6DeferredQueueResult")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd6DeferredQueueResult",
                Line);
        }
        if (A733LineContains(Line, "stageText=cmd6PreSdPortComplete")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd6PreSdPortComplete",
                Line);
        }
        if (A733LineContains(Line, "stageText=cmd6PostSdPortComplete")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd6PostSdPortComplete",
                Line);
        }
        if (A733LineContains(Line, "stageText=cmd6DpcIdentity")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd6DpcIdentity",
                Line);
        }
        if (A733LineContains(Line,
                             "stageText=cmd6StartTransferAlreadyComplete")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd6StartTransferAlreadyComplete",
                Line);
        }
        if (A733LineContains(Line, "stageText=cmd9CsdCapacityPatch")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd9CsdCapacityPatch",
                Line);
        }
        if (A733LineContains(Line, "stageText=cmd9CsdWriteProtectClear")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd9CsdWriteProtectClear",
                Line);
        }
        if (A733LineContains(Line, "stageText=readSplitSynthDataOverIdle")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:readSplitSynthDataOverIdle",
                Line);
        }
        if (A733LineContains(Line, "stageText=readSplitDataOverTimeoutState")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:readSplitDataOverTimeoutState",
                Line);
        }
        if (A733LineContains(Line, "stageText=readSplitDataOverResetPath")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:readSplitDataOverResetPath",
                Line);
        }
        if (A733LineContains(Line, "stageText=readSplitPioTimeoutState")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:readSplitPioTimeoutState",
                Line);
        }
        if (A733LineContains(Line, "stageText=lastGetResponse")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:lastGetResp6",
                Line);
        }
    }
    A733SummaryObserveCommandCombo(
        Entries,
        EntryCount,
        Line,
        "cmdName=CMD7_SELECT_CARD",
        "combo:issueCmd7",
        NULL,
        "combo:responseCmd7");
    A733SummaryObserveCommandCombo(
        Entries,
        EntryCount,
        Line,
        "cmdName=CMD8_SEND_IF_COND",
        "combo:issueCmd8",
        NULL,
        "combo:responseCmd8");
    A733SummaryObserveCommandCombo(
        Entries,
        EntryCount,
        Line,
        "cmdName=CMD9_SEND_CSD",
        "combo:issueCmd9",
        NULL,
        "combo:responseCmd9");
    A733SummaryObserveCommandCombo(
        Entries,
        EntryCount,
        Line,
        "cmdName=CMD12_STOP_TRANSMISSION",
        "combo:issueCmd12",
        NULL,
        NULL);
    A733SummaryObserveCommandCombo(
        Entries,
        EntryCount,
        Line,
        "cmdName=CMD13_SEND_STATUS",
        "combo:issueCmd13",
        NULL,
        "combo:responseCmd13");
    A733SummaryObserveCommandCombo(
        Entries,
        EntryCount,
        Line,
        "cmdName=CMD17_READ_SINGLE_BLOCK",
        "combo:issueCmd17",
        "combo:transferCmd17",
        NULL);
    if (A733LineContains(Line, "cmdName=CMD17_READ_SINGLE_BLOCK") &&
        A733LineContains(Line, " event=transfer ")) {
        if (A733LineContains(Line, "phase=0x00000001")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd17TransferStart",
                Line);
        }
        if (A733LineContains(Line, "phase=0x00000014")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd17TransferPioDone",
                Line);
        }
        if (A733LineContains(Line, "phase=0x00000017")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd17TransferSuccessExit",
                Line);
        }
        if (A733LineContains(Line, "phase=0x0000001A")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd17TransferWaitPending",
                Line);
        }
        if (A733LineContains(Line, "phase=0x0000001B")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd17TransferWaitTimeout",
                Line);
        }
        if (A733LineContains(Line, "phase=0x0000001C")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd17TransferWaitDataOver",
                Line);
        }
        if (A733LineContains(Line, "phase=0x00000028")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd17TransferIdle",
                Line);
        }
        if (A733LineContains(Line, "stageText=smallReadSynthTerminal")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd17TransferSynthTerminal",
                Line);
        }
        if (A733LineContains(Line, " arg=0x00000000")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd17TransferArg0",
                Line);
        }
        if (A733LineContains(Line, " arg=0x0EF6AFFF")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd17TransferArgTail",
                Line);
        }
        if (A733LineContains(Line, "stageText=synthAdvertisedTailRead")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd17TransferTailSynth",
                Line);
        }
    }
    A733SummaryObserveCommandCombo(
        Entries,
        EntryCount,
        Line,
        "cmdName=CMD18_READ_MULTIPLE_BLOCK",
        "combo:issueCmd18",
        "combo:transferCmd18",
        NULL);
    A733SummaryObserveCommandCombo(
        Entries,
        EntryCount,
        Line,
        "cmdName=CMD24_WRITE_SINGLE_BLOCK",
        "combo:issueCmd24",
        "combo:transferCmd24",
        NULL);
    A733SummaryObserveCommandCombo(
        Entries,
        EntryCount,
        Line,
        "cmdName=CMD25_WRITE_MULTIPLE_BLOCK",
        "combo:issueCmd25",
        "combo:transferCmd25",
        NULL);
    A733SummaryObserveCommandCombo(
        Entries,
        EntryCount,
        Line,
        "cmdName=ACMD41_SD_SEND_OP_COND",
        "combo:issueCmd41",
        NULL,
        "combo:responseCmd41");
    A733SummaryObserveCommandCombo(
        Entries,
        EntryCount,
        Line,
        "cmdName=ACMD42_SET_CLR_CARD_DETECT",
        "combo:issueCmd42",
        NULL,
        "combo:responseCmd42");
    A733SummaryObserveCommandCombo(
        Entries,
        EntryCount,
        Line,
        "cmdName=ACMD51_SEND_SCR",
        "combo:issueCmd51",
        "combo:transferCmd51",
        "combo:responseCmd51");
    A733SummaryObserveCommandCombo(
        Entries,
        EntryCount,
        Line,
        "cmdName=CMD55_APP_CMD",
        "combo:issueCmd55",
        NULL,
        "combo:responseCmd55");
    A733SummaryObserveCommandCombo(
        Entries,
        EntryCount,
        Line,
        "cmdName=CMD56_GEN_CMD",
        "combo:issueCmd56",
        NULL,
        NULL);
    if (A733LineContains(Line, "stageText=virtualSetupArg")) {
        BOOL Arg0;
        BOOL Arg10000;
        BOOL Block8;
        BOOL Block16;
        BOOL Cmd18;

        Arg0 = A733LineContains(Line, " arg=0x00000000");
        Arg10000 = A733LineContains(Line, " arg=0x00010000");
        Block8 = A733LineContains(Line, " blocks=0x00000008");
        Block16 = A733LineContains(Line, " blocks=0x00000010");
        Cmd18 = A733LineContains(Line, " cmd=0x00000012");

        if (A733LineContains(Line, " cmd=0x00000011")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:setupVirtCmd17",
                Line);
        }
        if (Cmd18) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:setupVirtCmd18",
                Line);
        }
        if (Arg0) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:setupVirtArg0",
                Line);
        }
        if (Arg10000) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:setupVirtArg10000",
                Line);
        }
        if (Cmd18 && Arg0) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:setupVirtCmd18Arg0",
                Line);
        }
        if (Cmd18 && Arg0 && Block8) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:setupVirtCmd18Arg0Block8",
                Line);
        }
        if (Cmd18 && Arg10000) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:setupVirtCmd18Arg10000",
                Line);
        }
        if (Cmd18 && Block8) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:setupVirtCmd18Block8",
                Line);
        }
        if (Cmd18 && Block16) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:setupVirtCmd18Block16",
                Line);
        }
        if (A733LineContains(Line, " hardwareArg=0x00810000")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:setupVirtHwSetupStart",
                Line);
        }
    }
    if (A733LineContains(Line, " cmd=0x00000012") &&
        A733LineContains(Line, " arg=0x00000000") &&
        A733LineContains(Line, " blocks=0x00000008")) {
        if (A733LineContains(Line, " event=issue ") &&
            A733LineContains(Line, " phase=0x00000001")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd18Arg0Blk8IssueEnter",
                Line);
        }
        if (A733LineContains(Line, " event=issue ") &&
            A733LineContains(Line, " phase=0x00000002")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd18Arg0Blk8IssueExit",
                Line);
        }
        if (A733LineContains(Line, " event=transfer ") &&
            A733LineContains(Line, " phase=0x00000001")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd18Arg0Blk8StartEntry",
                Line);
        }
        if (A733LineContains(Line, " event=transfer ") &&
            A733LineContains(Line, " phase=0x00000027")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd18Arg0Blk8StartNoHandoff",
                Line);
        }
        if (A733LineContains(Line, "stageText=readSplitBlockBegin")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd18Arg0Blk8ReadSplitBlock",
                Line);
        }
        if (A733LineContains(Line, "stageText=readSplitFinalQuiesce")) {
            A733SummaryIncrementByName(
                Entries,
                EntryCount,
                "combo:cmd18Arg0Blk8ReadSplitFinal",
                Line);
        }
    }
    if (A733LineContains(Line, "stageText=commandDoneResponse") &&
        A733LineContains(Line, "cmdName=CMD18_READ_MULTIPLE_BLOCK") &&
        A733LineContains(Line, " b=0x00000000")) {
        A733SummaryIncrementByName(
            Entries,
            EntryCount,
            "combo:cmd18Arg0Blk8CommandDone",
            Line);
    }
}

static DWORD
A733SummaryWriteLine(
    _In_ HANDLE Output,
    _Inout_ A733_LINE *Line,
    _In_z_ const CHAR *Operation)
{
    DWORD Error;

    if (!A733WriteLineToHandle(Output, Line, FALSE)) {
        Error = GetLastError();
        A733ReportError(Operation, Error);
        return Error;
    }
    return ERROR_SUCCESS;
}

static DWORD
A733SummaryWriteRecent(
    _In_ HANDLE Output,
    _In_reads_(A733_SUMMARY_RECENT_LINES) const A733_LINE *RecentLines,
    _In_ DWORD RecentCount,
    _In_ DWORD RecentNext)
{
    A733_LINE Line;
    DWORD Error;
    DWORD Index;
    DWORD RecentIndex;
    DWORD Start;

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_RECENT_BEGIN count=");
    A733LineAppendDecimal(&Line, RecentCount);
    A733LineAppendText(&Line, " capacity=");
    A733LineAppendDecimal(&Line, A733_SUMMARY_RECENT_LINES);
    Error = A733SummaryWriteLine(Output, &Line, "summary-write-recent-begin");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Start = (RecentCount == A733_SUMMARY_RECENT_LINES) ? RecentNext : 0U;
    for (Index = 0; Index < RecentCount; Index += 1U) {
        RecentIndex = Start + Index;
        if (RecentIndex >= A733_SUMMARY_RECENT_LINES) {
            RecentIndex -= A733_SUMMARY_RECENT_LINES;
        }
        A733ZeroBytes(&Line, sizeof(Line));
        A733LineAppendText(&Line, "A733_SMHC_RECENT ordinal=");
        A733LineAppendDecimal(&Line, Index);
        A733LineAppendChar(&Line, ' ');
        A733LineAppendBoundedBytes(
            &Line,
            RecentLines[RecentIndex].Data,
            RecentLines[RecentIndex].Length);
        Error = A733SummaryWriteLine(Output, &Line, "summary-write-recent");
        if (Error != ERROR_SUCCESS) {
            return Error;
        }
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_RECENT_END");
    return A733SummaryWriteLine(Output, &Line, "summary-write-recent-end");
}

static ULONGLONG
A733SummaryCountByName(
    _In_reads_(EntryCount) const A733_SUMMARY_ENTRY *Entries,
    _In_ DWORD EntryCount,
    _In_z_ const CHAR *Name)
{
    DWORD Index;

    for (Index = 0; Index < EntryCount; Index += 1) {
        if (A733StringEquals(Entries[Index].Name, Name)) {
            return Entries[Index].Count;
        }
    }
    return 0ULL;
}

static VOID
A733SummaryAppendKeyCount(
    _Inout_ A733_LINE *Line,
    _In_reads_(EntryCount) const A733_SUMMARY_ENTRY *Entries,
    _In_ DWORD EntryCount,
    _In_z_ const CHAR *Label,
    _In_z_ const CHAR *Name)
{
    A733LineAppendChar(Line, ' ');
    A733LineAppendText(Line, Label);
    A733LineAppendChar(Line, '=');
    A733LineAppendDecimal(Line, A733SummaryCountByName(Entries, EntryCount, Name));
}

static DWORD
A733SummaryWriteKeyFirst(
    _In_ HANDLE Output,
    _In_reads_(EntryCount) const A733_SUMMARY_ENTRY *Entries,
    _In_ DWORD EntryCount,
    _In_z_ const CHAR *Label,
    _In_z_ const CHAR *Name)
{
    A733_LINE Line;
    DWORD Index;

    for (Index = 0; Index < EntryCount; Index += 1) {
        if (A733StringEquals(Entries[Index].Name, Name)) {
            break;
        }
    }
    if ((Index == EntryCount) || !Entries[Index].HasFirst) {
        return ERROR_SUCCESS;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_KEY_FIRST ");
    A733LineAppendText(&Line, Label);
    A733LineAppendChar(&Line, ' ');
    A733LineAppendBoundedBytes(
        &Line,
        Entries[Index].FirstLine.Data,
        Entries[Index].FirstLine.Length);
    return A733SummaryWriteLine(Output, &Line, "summary-write-key-first");
}

static DWORD
A733SummaryWriteKeyLast(
    _In_ HANDLE Output,
    _In_reads_(EntryCount) const A733_SUMMARY_ENTRY *Entries,
    _In_ DWORD EntryCount,
    _In_z_ const CHAR *Label,
    _In_z_ const CHAR *Name)
{
    A733_LINE Line;
    DWORD Index;

    for (Index = 0; Index < EntryCount; Index += 1) {
        if (A733StringEquals(Entries[Index].Name, Name)) {
            break;
        }
    }
    if ((Index == EntryCount) || !Entries[Index].HasLast) {
        return ERROR_SUCCESS;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_KEY_LAST ");
    A733LineAppendText(&Line, Label);
    A733LineAppendChar(&Line, ' ');
    A733LineAppendBoundedBytes(
        &Line,
        Entries[Index].LastLine.Data,
        Entries[Index].LastLine.Length);
    return A733SummaryWriteLine(Output, &Line, "summary-write-key-last");
}

static DWORD
A733SummaryWriteLowArgOnly(
    _In_ HANDLE Output,
    _In_reads_(EntryCount) const A733_SUMMARY_ENTRY *Entries,
    _In_ DWORD EntryCount)
{
    A733_LINE Line;
    DWORD Error;

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_LOWARG");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "issue",
        "stageText=lowArgReadIssue");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "map",
        "stageText=lowArgReadMapped");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "cmd",
        "stageText=lowArgReadCmdWritten");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "pio",
        "stageText=lowArgReadPioDone");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "data",
        "stageText=lowArgReadData");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "rint",
        "stageText=lowArgReadRint");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "final",
        "stageText=lowArgReadFinal");
    Error = A733SummaryWriteLine(
        Output,
        &Line,
        "summary-write-lowarg-only");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_SETUPMAP");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "synthMbr",
        "stageText=synthProtectiveMbrLba0");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "virt",
        "stageText=virtualSetupArg");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "patchVbr",
        "stageText=patchSetupVbrHidden");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "synthVbr",
        "stageText=synthSetupVbrLowArg0");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "synthBoot16",
        "stageText=synthSetupBoot16LowArg0Start");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "synthBoot15",
        "stageText=synthSetupBootSector15LowArg");
    Error = A733SummaryWriteLine(
        Output,
        &Line,
        "summary-write-setupmap-only");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_SETUPDETAIL");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "cmd17",
        "combo:setupVirtCmd17");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "cmd18",
        "combo:setupVirtCmd18");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "arg0",
        "combo:setupVirtArg0");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "arg10000",
        "combo:setupVirtArg10000");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "cmd18Arg0",
        "combo:setupVirtCmd18Arg0");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "cmd18Arg0Blk8",
        "combo:setupVirtCmd18Arg0Block8");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "cmd18Arg10000",
        "combo:setupVirtCmd18Arg10000");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "cmd18Blk8",
        "combo:setupVirtCmd18Block8");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "cmd18Blk16",
        "combo:setupVirtCmd18Block16");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "hwSetupStart",
        "combo:setupVirtHwSetupStart");
    Error = A733SummaryWriteLine(
        Output,
        &Line,
        "summary-write-setupdetail-only");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_CMD18ARG0");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "issueEnter",
        "combo:cmd18Arg0Blk8IssueEnter");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "issueExit",
        "combo:cmd18Arg0Blk8IssueExit");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "cmdDone",
        "combo:cmd18Arg0Blk8CommandDone");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "dataOverResp",
        "stageText=cmd18Arg0DataOverAsResponse");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "resp",
        "stageText=synthSetupCmd18Arg0Response");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "handoff",
        "stageText=synthSetupCmd18Arg0SyncHandoff");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "fullDpc",
        "stageText=synthSetupCmd18Arg0FullDpc");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "fullHand",
        "stageText=synthSetupCmd18Arg0FullDpcHandoff");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "startData",
        "stageText=synthSetupCmd18Arg0StartData");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "synthDone",
        "stageText=synthSetupCmd18Arg0Block8");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "startEntry",
        "combo:cmd18Arg0Blk8StartEntry");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "noHandoff",
        "combo:cmd18Arg0Blk8StartNoHandoff");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "splitBlock",
        "combo:cmd18Arg0Blk8ReadSplitBlock");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "splitFinal",
        "combo:cmd18Arg0Blk8ReadSplitFinal");
    Error = A733SummaryWriteLine(
        Output,
        &Line,
        "summary-write-cmd18arg0-only");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_READSPLIT");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "block",
        "stageText=readSplitBlockBegin");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "pio",
        "stageText=readSplitPioDone");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "dataOver",
        "stageText=readSplitDataOver");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "deferred",
        "stageText=readSplitDeferredRwEnd");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "next",
        "stageText=readSplitNextCmdIssue");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "done",
        "stageText=readSplitNextCmdDone");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "synth",
        "stageText=readSplitSynthDataOverIdle");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "toState",
        "stageText=readSplitDataOverTimeoutState");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "reset",
        "stageText=readSplitDataOverResetPath");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "pioTo",
        "stageText=readSplitPioTimeoutState");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "final",
        "stageText=readSplitFinalQuiesce");
    Error = A733SummaryWriteLine(
        Output,
        &Line,
        "summary-write-readsplit-only");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "lowArgData",
        "stageText=lowArgReadData");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "mftData",
        "stageText=readSplitMftRecordData");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "logFileData",
        "stageText=readSplitLogFileRestartData");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "mftArgMap",
        "stageText=readSplitMftArgMap");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "partitionRelative",
        "stageText=partitionRelativeMode");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "virtualSetup",
        "stageText=virtualSetupArg");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "setupCmd18Arg0Blk8",
        "combo:setupVirtCmd18Arg0Block8");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "setupCmd18Arg10000",
        "combo:setupVirtCmd18Arg10000");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd18Arg0DataOverResp",
        "stageText=cmd18Arg0DataOverAsResponse");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd18Arg0SynthResp",
        "stageText=synthSetupCmd18Arg0Response");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd18Arg0SynthStart",
        "stageText=synthSetupCmd18Arg0StartData");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd18Arg0SynthHandoff",
        "stageText=synthSetupCmd18Arg0SyncHandoff");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd18Arg0SynthFullDpc",
        "stageText=synthSetupCmd18Arg0FullDpc");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd18Arg0SynthFullDpcHandoff",
        "stageText=synthSetupCmd18Arg0FullDpcHandoff");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd18Arg0SynthDone",
        "stageText=synthSetupCmd18Arg0Block8");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd18Arg0StartEntry",
        "combo:cmd18Arg0Blk8StartEntry");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd18Arg0NoHandoff",
        "combo:cmd18Arg0Blk8StartNoHandoff");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "patchVbr",
        "stageText=patchSetupVbrHidden");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "readSplitDataOverTimeout",
        "stageText=readSplitDataOverTimeoutState");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "readSplitPioTimeout",
        "stageText=readSplitPioTimeoutState");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "readSplitFinal",
        "stageText=readSplitFinalQuiesce");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    return A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "readSplitSynth",
        "stageText=readSplitSynthDataOverIdle");
}

static DWORD
A733SummaryWriteKeyCounts(
    _In_ HANDLE Output,
    _In_reads_(EntryCount) const A733_SUMMARY_ENTRY *Entries,
    _In_ DWORD EntryCount)
{
    A733_LINE Line;
    DWORD Error;

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_KEY redirect");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "begin",
        "stageText=redirectNtfsLogFileWriteBegin");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "scratchBlock",
        "stageText=redirectNtfsLogFileWriteScratchBlock");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "success",
        "stageText=redirectNtfsLogFileWriteSuccess");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "timeoutC0000103",
        "status=0xC0000103");
    Error = A733SummaryWriteLine(Output, &Line, "summary-write-key-redirect");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_KEY nonLogA733SetupWrite");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "stallBegin",
        "stageText=nonLogA733SetupWriteStallBegin");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "stallEnd",
        "stageText=nonLogA733SetupWriteStallEnd");
    Error = A733SummaryWriteLine(
        Output,
        &Line,
        "summary-write-key-nonlog-a733setup");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_KEY cmd7R1B");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "tranResponse",
        "stageText=cmd7R1BTranResponse");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "raw700",
        "rawResponse=0x00000700");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "normalized900",
        "normalizedResponse=0x00000900");
    Error = A733SummaryWriteLine(Output, &Line, "summary-write-key-cmd7-r1b");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_KEY timeout");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "ioTimeout",
        "status=0xC00000B5");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "requestIoTimeout",
        "requestStatus=0xC00000B5");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "transferIoTimeout",
        "combo:transferIoTimeout");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "issueIoTimeout",
        "combo:issueIoTimeout");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "dpcIoTimeout",
        "combo:dpcIoTimeout");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "lifeIoTimeout",
        "combo:lifecycleIoTimeout");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "smallPostIoTimeout",
        "combo:smallReadPostIoTimeout");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "lastCmdIoTimeout",
        "combo:lastCommandEventsIoTimeout");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "lastResetDat",
        "combo:lastResetDat");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "lastResetCmd",
        "combo:lastResetCmd");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "lastResetAll",
        "combo:lastResetAll");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "lastResetDatCmd7",
        "combo:lastResetDatCmd7");
    Error = A733SummaryWriteLine(Output, &Line, "summary-write-key-timeout");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_KEY events");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "bus", "bus");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "lifecycle", "lifecycle");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "issue", "issue");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "transfer", "transfer");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "response", "response");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "completion", "completion");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "recovery", "recovery");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "card", "card");
    Error = A733SummaryWriteLine(Output, &Line, "summary-write-key-events");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_KEY flow");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "issueEnter",
        "stageText=issueRequestEnter");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "issueBusy",
        "stageText=issueRequestBusy");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "issueExit",
        "stageText=issueRequestExit");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "resetSnapshot",
        "stageText=resetSnapshot");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "lastCmdForReset",
        "stageText=lastCommandForReset");
    Error = A733SummaryWriteLine(Output, &Line, "summary-write-key-flow");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_KEY cmds");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "issue6", "combo:issueCmd6");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "issue7", "combo:issueCmd7");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "issue13", "combo:issueCmd13");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "issue17", "combo:issueCmd17");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "issue18", "combo:issueCmd18");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "issue24", "combo:issueCmd24");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "issue25", "combo:issueCmd25");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "issue42", "combo:issueCmd42");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "xfer17", "combo:transferCmd17");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "xfer18", "combo:transferCmd18");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "xfer24", "combo:transferCmd24");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "xfer25", "combo:transferCmd25");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "resp7", "combo:responseCmd7");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "resp6", "combo:responseCmd6");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "resp13", "combo:responseCmd13");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "resp42", "combo:responseCmd42");
    Error = A733SummaryWriteLine(Output, &Line, "summary-write-key-cmds");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_KEY initIssue");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd0", "combo:issueCmd0");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd2", "combo:issueCmd2");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd3", "combo:issueCmd3");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6", "combo:issueCmd6");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd7", "combo:issueCmd7");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd8", "combo:issueCmd8");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd9", "combo:issueCmd9");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd12", "combo:issueCmd12");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd13", "combo:issueCmd13");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "acmd41", "combo:issueCmd41");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "acmd42", "combo:issueCmd42");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "acmd51", "combo:issueCmd51");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd55", "combo:issueCmd55");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd56", "combo:issueCmd56");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6NoX", "combo:issueCmd6NoTransfer");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6Xfer", "combo:issueCmd6Transfer");
    Error = A733SummaryWriteLine(Output, &Line, "summary-write-key-init-issue");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_KEY initResp");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd2", "combo:responseCmd2");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd3", "combo:responseCmd3");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6", "combo:responseCmd6");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd7", "combo:responseCmd7");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd8", "combo:responseCmd8");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd9", "combo:responseCmd9");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd13", "combo:responseCmd13");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "acmd41", "combo:responseCmd41");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "acmd42", "combo:responseCmd42");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "acmd51", "combo:responseCmd51");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd55", "combo:responseCmd55");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "xfer51", "combo:transferCmd51");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "xfer6", "combo:transferCmd6");
    Error = A733SummaryWriteLine(Output, &Line, "summary-write-key-init-resp");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_KEY diag");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "synthTerm", "stageText=smallReadSynthTerminal");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "synthReset", "stageText=smallReadSynthResetFifo");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "synthDat", "stageText=smallReadSynthResetDataPath");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "lowIssue", "stageText=lowArgReadIssue");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "lowMap", "stageText=lowArgReadMapped");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "lowPio", "stageText=lowArgReadPioDone");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "lowData", "stageText=lowArgReadData");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "lowFinal", "stageText=lowArgReadFinal");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "arg0Dor", "stageText=cmd18Arg0DataOverAsResponse");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "arg0Resp", "stageText=synthSetupCmd18Arg0Response");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "arg0Start", "stageText=synthSetupCmd18Arg0StartData");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "arg0Hand", "stageText=synthSetupCmd18Arg0SyncHandoff");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "arg0Full", "stageText=synthSetupCmd18Arg0FullDpc");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "arg0FullH", "stageText=synthSetupCmd18Arg0FullDpcHandoff");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "arg0Done", "stageText=synthSetupCmd18Arg0Block8");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmdPre", "stageText=cmdStartPrewait");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "hleBeg", "stageText=readHleRetryBegin");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "hlePio", "stageText=readHleRetryPio");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "hleDone", "stageText=readHleRetryDone");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "smallForce", "stageText=smallReadForceCompletionRint");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "synthMbr", "stageText=synthProtectiveMbrLba0");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "virt", "stageText=virtualSetupArg");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "patchVbr", "stageText=patchSetupVbrHidden");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "backupVbr", "stageText=setupBackupVbrRemap");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "logFile", "stageText=readSplitLogFileRestartData");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "fakeNonLogWr", "stageText=fakeNonLogA733SetupWriteSuccess");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "synthScr", "stageText=syntheticAcmd51Scr");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "synthScrX", "stageText=syntheticAcmd51StartTransfer");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmdDone9", "combo:cmdDoneResp9");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmdDone42", "combo:cmdDoneResp42");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmdDoneR2", "combo:cmdDoneMetaR2");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "lastGet9", "combo:lastGetResp9");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "lastGet42", "combo:lastGetResp42");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd2Raw", "combo:cmd2R2Raw");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd2Out", "combo:cmd2R2Out");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6Norm", "combo:cmd6R1ForceTran");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6ErrClr", "combo:cmd6IgnoredCommandErrors");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6Plan", "stageText=cmd6IssuePlan");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6Regs", "stageText=cmd6IssueRegs");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6Irq", "stageText=cmd6IssueRespIrq");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6Data", "stageText=cmd6IssueDataRegs");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6Synth", "combo:cmd6SyntheticStatus");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6Queue", "combo:cmd6DeferredQueueResult");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6Pre", "combo:cmd6PreSdPortComplete");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6Post", "combo:cmd6PostSdPortComplete");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6StartOk", "combo:cmd6StartTransferAlreadyComplete");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd9Patch", "combo:cmd9CsdCapacityPatch");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd9WpClr", "combo:cmd9CsdWriteProtectClear");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "readSynth", "combo:readSplitSynthDataOverIdle");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "readToState", "combo:readSplitDataOverTimeoutState");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "readReset", "combo:readSplitDataOverResetPath");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "readPioTo", "combo:readSplitPioTimeoutState");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd9Raw", "combo:cmd9R2Raw");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd9Out", "combo:cmd9R2Out");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "scrWords", "combo:syntheticScrWords");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "appClr", "combo:acmdR1AppCmdClear");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "lastCmd", "stageText=lastCommandDetail");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "lastCmdEv", "stageText=lastCommandDetailEvents");
    Error = A733SummaryWriteLine(Output, &Line, "summary-write-key-diag");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_KEY bus");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "resetHw", "busName=SdResetHw");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "resetHost", "busName=SdResetHost");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "clock", "busName=SdSetClock");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "clockNoHw", "stageText=setClockAcceptedNoHwUpdate");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "voltage", "busName=SdSetVoltage");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "power", "busName=SdSetPower");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "width", "busName=SdSetBusWidth");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "speed", "busName=SdSetBusSpeed");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "sigVolt", "busName=SdSetSignalingVoltage");
    Error = A733SummaryWriteLine(Output, &Line, "summary-write-key-bus");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_KEY busValue");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "resetAll", "busValueName=SdResetTypeAll");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "resetCmd", "busValueName=SdResetTypeCmd");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "resetDat", "busValueName=SdResetTypeDat");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "volt33", "busValueName=SdBusVoltage33");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "widthUndef", "busValueName=SdBusWidthUndefined");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "width1", "busValueName=SdBusWidth1Bit");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "width4", "busValueName=SdBusWidth4Bit");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "speedNormal", "busValueName=SdBusSpeedNormal");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "speedHigh", "busValueName=SdBusSpeedHigh");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "sig33", "busValueName=SdSignalingVoltage33");
    Error = A733SummaryWriteLine(Output, &Line, "summary-write-key-bus-value");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd6Issue",
        "combo:issueCmd6");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd6Transfer",
        "combo:transferCmd6");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd6Resp",
        "combo:responseCmd6");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_KEY busStatus");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "success", "combo:busStatusSuccess");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "notSupported", "combo:busStatusNotSupported");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "invalid", "combo:busStatusInvalidParameter");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "timeout", "combo:busStatusIoTimeout");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "pending", "combo:busStatusPending");
    Error = A733SummaryWriteLine(Output, &Line, "summary-write-key-bus-status");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_KEY smallRead");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "req", "stageText=smallReadRequest");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "req51", "combo:smallReadReqCmd51");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "data", "stageText=smallReadData");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "post", "stageText=smallReadPostCompletion");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "state", "stageText=smallReadCompletionState");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "shape", "stageText=smallReadShapeAndRegs");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "postRegs", "stageText=smallReadPostRegs");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "respLow", "stageText=smallReadRespRegsLow");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "respHigh", "stageText=smallReadRespRegsHigh");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "lowIssue", "stageText=lowArgReadIssue");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "lowMap", "stageText=lowArgReadMapped");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "lowCmd", "stageText=lowArgReadCmdWritten");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "lowPio", "stageText=lowArgReadPioDone");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "lowData", "stageText=lowArgReadData");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "lowRint", "stageText=lowArgReadRint");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "lowFinal", "stageText=lowArgReadFinal");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "logFile", "stageText=readSplitLogFileRestartData");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "synthMbr", "stageText=synthProtectiveMbrLba0");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "virt", "stageText=virtualSetupArg");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "patchVbr", "stageText=patchSetupVbrHidden");
    Error = A733SummaryWriteLine(Output, &Line, "summary-write-key-small-read");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_KEY cmd17Phase");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "start", "combo:cmd17TransferStart");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "pio", "combo:cmd17TransferPioDone");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "ok", "combo:cmd17TransferSuccessExit");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "wait", "combo:cmd17TransferWaitPending");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "to", "combo:cmd17TransferWaitTimeout");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "dataOver", "combo:cmd17TransferWaitDataOver");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "idle", "combo:cmd17TransferIdle");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "synth", "combo:cmd17TransferSynthTerminal");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "force", "stageText=smallReadForceCompletionRint");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "arg0", "combo:cmd17TransferArg0");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "tail", "combo:cmd17TransferArgTail");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "tailSynth", "combo:cmd17TransferTailSynth");
    Error = A733SummaryWriteLine(Output, &Line, "summary-write-key-cmd17-phase");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd17Timeout",
        "combo:cmd17TransferWaitTimeout");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd17Pending",
        "combo:cmd17TransferWaitPending");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd17Ok",
        "combo:cmd17TransferSuccessExit");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd17Tail",
        "combo:cmd17TransferArgTail");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd17TailSynth",
        "stageText=synthAdvertisedTailRead");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "setupBackupVbr",
        "stageText=setupBackupVbrRemap");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "smallReadPost",
        "stageText=smallReadPostCompletion");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "lowArgMap",
        "stageText=lowArgReadMapped");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "lowArgData",
        "stageText=lowArgReadData");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "mftData",
        "stageText=readSplitMftRecordData");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "logFileData",
        "stageText=readSplitLogFileRestartData");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "mftArgMap",
        "stageText=readSplitMftArgMap");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "partitionRelative",
        "stageText=partitionRelativeMode");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "lowArgFinal",
        "stageText=lowArgReadFinal");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "lastCmd",
        "stageText=lastCommandDetail");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "lastCmdEvents",
        "stageText=lastCommandDetailEvents");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd2Raw",
        "combo:cmd2R2Raw");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd2Out",
        "combo:cmd2R2Out");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd9Raw",
        "combo:cmd9R2Raw");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd9Out",
        "combo:cmd9R2Out");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "scrWords",
        "combo:syntheticScrWords");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "appClr",
        "combo:acmdR1AppCmdClear");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "busAll",
        "bus");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "busResetHost",
        "busName=SdResetHost");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "busClock",
        "busName=SdSetClock");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "busVoltage",
        "busName=SdSetVoltage");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "busWidth",
        "busName=SdSetBusWidth");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "busNotSupported",
        "combo:busStatusNotSupported");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "busInvalid",
        "combo:busStatusInvalidParameter");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_KEY scratchWrite");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "reset",
        "stageText=redirectScratchWriteResetFifo");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "issue",
        "stageText=redirectScratchWriteIssueCmd");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "startReady",
        "stageText=redirectScratchWriteStartReady");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "rintErr",
        "stageText=redirectScratchWriteRintError");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "pio",
        "stageText=redirectScratchWritePioWrite");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "postPioReady",
        "stageText=redirectScratchWritePostPioReady");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "dataOver",
        "stageText=redirectScratchWriteDataOver");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "idle",
        "stageText=redirectScratchWriteDataPathIdle");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "resetAfter",
        "stageText=redirectScratchWriteResetFifoAfter");
    Error = A733SummaryWriteLine(Output, &Line, "summary-write-key-scratch-write");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_KEY scratchRead");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "reset",
        "stageText=redirectScratchReadResetFifo");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "issue",
        "stageText=redirectScratchReadIssueCmd");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "cmdDone",
        "stageText=redirectScratchReadCmdDone");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "rintErr",
        "stageText=redirectScratchReadRintError");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "pio",
        "stageText=redirectScratchReadPioRead");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "dataOver",
        "stageText=redirectScratchReadDataOver");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "idle",
        "stageText=redirectScratchReadDataPathIdle");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "resetAfter",
        "stageText=redirectScratchReadResetFifoAfter");
    Error = A733SummaryWriteLine(Output, &Line, "summary-write-key-scratch-read");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_KEY scratchVerify");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "mismatch",
        "stageText=redirectScratchVerifyMismatch");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "expected",
        "stageText=redirectScratchVerifyExpectedChecksum");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "actual",
        "stageText=redirectScratchVerifyActualChecksum");
    A733SummaryAppendKeyCount(
        &Line,
        Entries,
        EntryCount,
        "success",
        "stageText=redirectScratchVerifySuccess");
    Error = A733SummaryWriteLine(Output, &Line, "summary-write-key-scratch-verify");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_KEY frontier");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "issue6", "combo:issueCmd6");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6NoX", "combo:issueCmd6NoTransfer");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6Xfer", "combo:issueCmd6Transfer");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "xfer6", "combo:transferCmd6");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "resp6", "combo:responseCmd6");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6Norm", "combo:cmd6R1ForceTran");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6ErrClr", "combo:cmd6IgnoredCommandErrors");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6Plan", "stageText=cmd6IssuePlan");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6Regs", "stageText=cmd6IssueRegs");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6Irq", "stageText=cmd6IssueRespIrq");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6Data", "stageText=cmd6IssueDataRegs");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6Synth", "combo:cmd6SyntheticStatus");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6Queue", "combo:cmd6DeferredQueueResult");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6Pre", "combo:cmd6PreSdPortComplete");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6Post", "combo:cmd6PostSdPortComplete");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd6StartOk", "combo:cmd6StartTransferAlreadyComplete");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd9Patch", "combo:cmd9CsdCapacityPatch");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd9WpClr", "combo:cmd9CsdWriteProtectClear");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "readSynth", "combo:readSplitSynthDataOverIdle");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "readToState", "combo:readSplitDataOverTimeoutState");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "readReset", "combo:readSplitDataOverResetPath");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "readPioTo", "combo:readSplitPioTimeoutState");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "issue13", "combo:issueCmd13");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "issue17", "combo:issueCmd17");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "issue18", "combo:issueCmd18");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "issue24", "combo:issueCmd24");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "issue25", "combo:issueCmd25");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd2Raw", "combo:cmd2R2Raw");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd2Out", "combo:cmd2R2Out");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd9Raw", "combo:cmd9R2Raw");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "cmd9Out", "combo:cmd9R2Out");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "scrWords", "combo:syntheticScrWords");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "widthUndef", "busValueName=SdBusWidthUndefined");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "width1", "busValueName=SdBusWidth1Bit");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "width4", "busValueName=SdBusWidth4Bit");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "busBad", "combo:busStatusNotSupported");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "busInvalid", "combo:busStatusInvalidParameter");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "busTimeout", "combo:busStatusIoTimeout");
    Error = A733SummaryWriteLine(Output, &Line, "summary-write-key-frontier");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd6IssueTail",
        "combo:issueCmd6");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd6TransferTail",
        "combo:transferCmd6");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd6RespTail",
        "combo:responseCmd6");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd6IssuePlan",
        "stageText=cmd6IssuePlan");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd6IssueEvents",
        "stageText=cmd6IssueEvents");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd6IssueRegs",
        "stageText=cmd6IssueRegs");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd6IssueRespIrq",
        "stageText=cmd6IssueRespIrq");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd6IssueDataRegs",
        "stageText=cmd6IssueDataRegs");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd6SyntheticStatus",
        "stageText=cmd6SyntheticStatus");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd6DeferredQueueResult",
        "stageText=cmd6DeferredQueueResult");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd6PreSdPortComplete",
        "stageText=cmd6PreSdPortComplete");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd6PostSdPortComplete",
        "stageText=cmd6PostSdPortComplete");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd6DpcIdentity",
        "stageText=cmd6DpcIdentity");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd6StartTransferAlreadyComplete",
        "stageText=cmd6StartTransferAlreadyComplete");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "cmd9CsdCapacityPatch",
        "stageText=cmd9CsdCapacityPatch");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_KEY cmd6Complete");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "queue", "combo:cmd6DeferredQueued");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "enter", "combo:cmd6DeferredEnter");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "exit", "combo:cmd6DeferredExit");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "err", "combo:cmd6DeferredError");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "synth", "combo:cmd6SyntheticStatus");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "qTrace", "combo:cmd6DeferredQueueResult");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "pre", "combo:cmd6PreSdPortComplete");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "post", "combo:cmd6PostSdPortComplete");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "ident", "combo:cmd6DpcIdentity");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "startOk", "combo:cmd6StartTransferAlreadyComplete");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "getResp", "combo:lastGetResp6");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "resp", "combo:responseCmd6");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "xfer", "combo:transferCmd6");
    Error = A733SummaryWriteLine(Output, &Line, "summary-write-key-cmd6-complete");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_KEY postCmd6");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "issue13", "combo:issueCmd13");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "issue17", "combo:issueCmd17");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "issue18", "combo:issueCmd18");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "issue24", "combo:issueCmd24");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "issue25", "combo:issueCmd25");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "xfer17", "combo:transferCmd17");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "xfer18", "combo:transferCmd18");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "xfer24", "combo:transferCmd24");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "xfer25", "combo:transferCmd25");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "resp13", "combo:responseCmd13");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "resetHost", "busName=SdResetHost");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "resetDat", "busValueName=SdResetTypeDat");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "smallPost", "stageText=smallReadPostCompletion");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "busTimeout", "combo:busStatusIoTimeout");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "speedHigh", "busValueName=SdBusSpeedHigh");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "busBad", "combo:busStatusNotSupported");
    Error = A733SummaryWriteLine(Output, &Line, "summary-write-key-post-cmd6");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_KEY timeout_tail");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "ioTimeout", "status=0xC00000B5");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "requestIoTimeout", "requestStatus=0xC00000B5");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "transferIoTimeout", "combo:transferIoTimeout");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "issueIoTimeout", "combo:issueIoTimeout");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "dpcIoTimeout", "combo:dpcIoTimeout");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "lifeIoTimeout", "combo:lifecycleIoTimeout");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "smallPostIoTimeout", "combo:smallReadPostIoTimeout");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "lastCmdIoTimeout", "combo:lastCommandEventsIoTimeout");
    A733SummaryAppendKeyCount(&Line, Entries, EntryCount, "lastResetDat", "combo:lastResetDat");
    Error = A733SummaryWriteLine(Output, &Line, "summary-write-key-timeout-tail");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyFirst(
        Output,
        Entries,
        EntryCount,
        "issueCmd25",
        "combo:issueCmd25");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "issueCmd25",
        "combo:issueCmd25");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyFirst(
        Output,
        Entries,
        EntryCount,
        "transferCmd25",
        "combo:transferCmd25");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "transferCmd25",
        "combo:transferCmd25");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyFirst(
        Output,
        Entries,
        EntryCount,
        "ioTimeout",
        "status=0xC00000B5");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "ioTimeout",
        "status=0xC00000B5");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyFirst(
        Output,
        Entries,
        EntryCount,
        "transferIoTimeout",
        "combo:transferIoTimeout");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "transferIoTimeout",
        "combo:transferIoTimeout");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyFirst(
        Output,
        Entries,
        EntryCount,
        "issueIoTimeout",
        "combo:issueIoTimeout");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "issueIoTimeout",
        "combo:issueIoTimeout");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    Error = A733SummaryWriteKeyFirst(
        Output,
        Entries,
        EntryCount,
        "requestIoTimeout",
        "requestStatus=0xC00000B5");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    return A733SummaryWriteKeyLast(
        Output,
        Entries,
        EntryCount,
        "requestIoTimeout",
        "requestStatus=0xC00000B5");
}

static DWORD
A733SummaryWrite(
    _In_ HANDLE Output,
    _In_reads_(EntryCount) const A733_SUMMARY_ENTRY *Entries,
    _In_ DWORD EntryCount,
    _In_reads_(A733_SUMMARY_RECENT_LINES) const A733_LINE *RecentLines,
    _In_ DWORD RecentCount,
    _In_ DWORD RecentNext)
{
    A733_LINE Line;
    DWORD Error;
    DWORD Index;

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_SUMMARY status=BEGIN");
    Error = A733SummaryWriteLine(Output, &Line, "summary-write-begin");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_SUMMARY counts=split entryCount=");
    A733LineAppendDecimal(&Line, EntryCount);
    Error = A733SummaryWriteLine(Output, &Line, "summary-write-counts");
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    for (Index = 0; Index < EntryCount; Index += 1) {
        if ((Entries[Index].Count == 0ULL) &&
            (Index < ARRAYSIZE(A733_EVENT_SCHEMAS))) {
            continue;
        }
        A733ZeroBytes(&Line, sizeof(Line));
        A733LineAppendText(&Line, "A733_SMHC_COUNT name=");
        A733LineAppendText(&Line, Entries[Index].Name);
        A733LineAppendText(&Line, " count=");
        A733LineAppendDecimal(&Line, Entries[Index].Count);
        Error = A733SummaryWriteLine(
            Output,
            &Line,
            "summary-write-count-entry");
        if (Error != ERROR_SUCCESS) {
            return Error;
        }
    }

    for (Index = 0; Index < EntryCount; Index += 1) {
        if (!Entries[Index].HasLast) {
            continue;
        }
        A733ZeroBytes(&Line, sizeof(Line));
        A733LineAppendText(&Line, "A733_SMHC_LAST event=");
        A733LineAppendText(&Line, Entries[Index].Name);
        A733LineAppendChar(&Line, ' ');
        A733LineAppendBoundedBytes(
            &Line,
            Entries[Index].LastLine.Data,
            Entries[Index].LastLine.Length);
        Error = A733SummaryWriteLine(Output, &Line, "summary-write-last");
        if (Error != ERROR_SUCCESS) {
            return Error;
        }
    }

    Error = A733SummaryWriteRecent(
        Output,
        RecentLines,
        RecentCount,
        RecentNext);
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    Error = A733SummaryWriteKeyCounts(Output, Entries, EntryCount);
    if (Error != ERROR_SUCCESS) {
        return Error;
    }

    A733ZeroBytes(&Line, sizeof(Line));
    A733LineAppendText(&Line, "A733_SMHC_SUMMARY status=END");
    return A733SummaryWriteLine(Output, &Line, "summary-write-end");
}

static DWORD
A733Summary(
    _In_z_ const WCHAR *LogPath,
    _In_ BOOL KeyOnly,
    _In_ BOOL LowArgOnly)
{
    A733_SUMMARY_ENTRY *Entries;
    A733_LINE CurrentLine;
    A733_LINE *RecentLines;
    DWORD Error;
    DWORD EntryCount;
    DWORD Index;
    DWORD Read;
    DWORD RecentCount;
    DWORD RecentNext;
    HANDLE File;
    HANDLE Output;

    Output = GetStdHandle(STD_OUTPUT_HANDLE);
    if ((Output == NULL) || (Output == INVALID_HANDLE_VALUE)) {
        Error = ERROR_INVALID_HANDLE;
        A733ReportError("summary-stdout", Error);
        return Error;
    }

    EntryCount = ARRAYSIZE(A733_EVENT_SCHEMAS) +
                 ARRAYSIZE(A733_SUMMARY_MARKERS);
    Entries = (A733_SUMMARY_ENTRY *)HeapAlloc(
        GetProcessHeap(),
        0,
        sizeof(A733_SUMMARY_ENTRY) * EntryCount);
    if (Entries == NULL) {
        Error = ERROR_NOT_ENOUGH_MEMORY;
        A733ReportError("summary-alloc", Error);
        return Error;
    }
    RecentLines = (A733_LINE *)HeapAlloc(
        GetProcessHeap(),
        0,
        sizeof(A733_LINE) * A733_SUMMARY_RECENT_LINES);
    if (RecentLines == NULL) {
        Error = ERROR_NOT_ENOUGH_MEMORY;
        A733ReportError("summary-recent-alloc", Error);
        (VOID)HeapFree(GetProcessHeap(), 0, Entries);
        return Error;
    }
    A733ZeroBytes(Entries, sizeof(A733_SUMMARY_ENTRY) * EntryCount);
    A733ZeroBytes(
        RecentLines,
        sizeof(A733_LINE) * A733_SUMMARY_RECENT_LINES);
    RecentCount = 0;
    RecentNext = 0;
    for (Index = 0; Index < ARRAYSIZE(A733_EVENT_SCHEMAS); Index += 1) {
        Entries[Index].Name = A733_EVENT_SCHEMAS[Index].Name;
    }
    for (Index = 0; Index < ARRAYSIZE(A733_SUMMARY_MARKERS); Index += 1) {
        Entries[ARRAYSIZE(A733_EVENT_SCHEMAS) + Index].Name =
            A733_SUMMARY_MARKERS[Index];
    }

    File = CreateFileW(
        LogPath,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (File == INVALID_HANDLE_VALUE) {
        Error = GetLastError();
        A733ReportError("summary-open", Error);
        (VOID)HeapFree(GetProcessHeap(), 0, RecentLines);
        (VOID)HeapFree(GetProcessHeap(), 0, Entries);
        return Error;
    }

    Error = ERROR_SUCCESS;
    A733ZeroBytes(&CurrentLine, sizeof(CurrentLine));
    for (;;) {
        Read = 0;
        if (!ReadFile(File,
                      A733_DumpBuffer,
                      A733_DUMP_CHUNK_BYTES,
                      &Read,
                      NULL)) {
            Error = GetLastError();
            A733ReportError("summary-read", Error);
            break;
        }
        if (Read == 0) {
            break;
        }
        for (Index = 0; Index < Read; Index += 1) {
            if (A733_DumpBuffer[Index] == (BYTE)'\n') {
                if (CurrentLine.Length != 0) {
                    A733SummaryObserveLine(
                        Entries,
                        EntryCount,
                        &CurrentLine);
                    A733RecentRememberLine(
                        RecentLines,
                        &RecentCount,
                        &RecentNext,
                        &CurrentLine);
                }
                A733ZeroBytes(&CurrentLine, sizeof(CurrentLine));
            } else if (A733_DumpBuffer[Index] != (BYTE)'\r') {
                A733LineAppendChar(
                    &CurrentLine,
                    (CHAR)A733_DumpBuffer[Index]);
            }
        }
    }

    if ((Error == ERROR_SUCCESS) && (CurrentLine.Length != 0)) {
        A733SummaryObserveLine(Entries, EntryCount, &CurrentLine);
        A733RecentRememberLine(
            RecentLines,
            &RecentCount,
            &RecentNext,
            &CurrentLine);
    }
    (VOID)CloseHandle(File);
    if (Error != ERROR_SUCCESS) {
        (VOID)HeapFree(GetProcessHeap(), 0, RecentLines);
        (VOID)HeapFree(GetProcessHeap(), 0, Entries);
        return Error;
    }
    if (LowArgOnly) {
        Error = A733SummaryWriteLowArgOnly(Output, Entries, EntryCount);
    } else if (KeyOnly) {
        Error = A733SummaryWriteKeyCounts(Output, Entries, EntryCount);
    } else {
        Error = A733SummaryWrite(
            Output,
            Entries,
            EntryCount,
            RecentLines,
            RecentCount,
            RecentNext);
    }
    (VOID)HeapFree(GetProcessHeap(), 0, RecentLines);
    (VOID)HeapFree(GetProcessHeap(), 0, Entries);
    return Error;
}

static DWORD
A733VerifyExactFile(
    _In_z_ const WCHAR *Path,
    _In_reads_bytes_(ExpectedLength) const CHAR *Expected,
    _In_ DWORD ExpectedLength)
{
    HANDLE File;
    LARGE_INTEGER Size;
    CHAR Buffer[128];
    DWORD Read;
    DWORD Index;
    DWORD Error;

    if (ExpectedLength > sizeof(Buffer)) {
        return ERROR_INSUFFICIENT_BUFFER;
    }
    File = CreateFileW(
        Path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (File == INVALID_HANDLE_VALUE) {
        return GetLastError();
    }
    if (!GetFileSizeEx(File, &Size)) {
        Error = GetLastError();
        (VOID)CloseHandle(File);
        return Error;
    }
    if ((ULONGLONG)Size.QuadPart != (ULONGLONG)ExpectedLength) {
        (VOID)CloseHandle(File);
        return ERROR_INVALID_DATA;
    }
    Read = 0;
    if (!ReadFile(File, Buffer, ExpectedLength, &Read, NULL)) {
        Error = GetLastError();
        (VOID)CloseHandle(File);
        return Error;
    }
    (VOID)CloseHandle(File);
    if (Read != ExpectedLength) {
        return ERROR_HANDLE_EOF;
    }
    for (Index = 0; Index < ExpectedLength; Index += 1) {
        if (Buffer[Index] != Expected[Index]) {
            return ERROR_INVALID_DATA;
        }
    }
    return ERROR_SUCCESS;
}

static DWORD
A733VerifyFileContains(
    _In_z_ const WCHAR *Path,
    _In_z_ const CHAR *Needle)
{
    HANDLE File;
    BYTE *Buffer;
    DWORD NeedleLength;
    DWORD Carry;
    DWORD Read;
    DWORD Total;
    DWORD Offset;
    DWORD Index;
    DWORD Error;
    BOOL Found;

    NeedleLength = 0;
    while (Needle[NeedleLength] != '\0') {
        NeedleLength += 1;
    }
    if (NeedleLength == 0) {
        return ERROR_INVALID_PARAMETER;
    }
    Buffer = (BYTE *)HeapAlloc(
        GetProcessHeap(),
        0,
        A733_DUMP_CHUNK_BYTES + NeedleLength);
    if (Buffer == NULL) {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    File = CreateFileW(
        Path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (File == INVALID_HANDLE_VALUE) {
        Error = GetLastError();
        (VOID)HeapFree(GetProcessHeap(), 0, Buffer);
        return Error;
    }

    Carry = 0;
    Found = FALSE;
    Error = ERROR_SUCCESS;
    for (;;) {
        Read = 0;
        if (!ReadFile(File,
                      Buffer + Carry,
                      A733_DUMP_CHUNK_BYTES,
                      &Read,
                      NULL)) {
            Error = GetLastError();
            break;
        }
        Total = Carry + Read;
        if (Total >= NeedleLength) {
            for (Offset = 0;
                 Offset <= (Total - NeedleLength);
                 Offset += 1) {
                for (Index = 0; Index < NeedleLength; Index += 1) {
                    if (Buffer[Offset + Index] != (BYTE)Needle[Index]) {
                        break;
                    }
                }
                if (Index == NeedleLength) {
                    Found = TRUE;
                    break;
                }
            }
        }
        if (Found || (Read == 0)) {
            break;
        }
        Carry = (Total < (NeedleLength - 1U)) ?
                Total : (NeedleLength - 1U);
        for (Index = 0; Index < Carry; Index += 1) {
            Buffer[Index] = Buffer[Total - Carry + Index];
        }
    }
    (VOID)CloseHandle(File);
    (VOID)HeapFree(GetProcessHeap(), 0, Buffer);
    if (Error != ERROR_SUCCESS) {
        return Error;
    }
    return Found ? ERROR_SUCCESS : ERROR_NOT_FOUND;
}

static DWORD
A733Verify(
    _In_z_ const WCHAR *LogPath,
    _In_z_ const WCHAR *DonePath)
{
    static const CHAR ExpectedDone[] =
        "A733SmhcTrace done status=0x00000000\r\n";
    static const CHAR LifecycleNeedle[] =
        " event=lifecycle id=1 ";
    DWORD Error;

    Error = A733VerifyExactFile(
        DonePath,
        ExpectedDone,
        (DWORD)(ARRAYSIZE(ExpectedDone) - 1U));
    if (Error != ERROR_SUCCESS) {
        A733ReportError("verify-done", Error);
        return Error;
    }
    Error = A733VerifyFileContains(LogPath, LifecycleNeedle);
    if (Error != ERROR_SUCCESS) {
        A733ReportError("verify-lifecycle", Error);
        return Error;
    }
    A733WriteConsoleText(
        STD_OUTPUT_HANDLE,
        "A733_SMHC_TRACE_VERIFY status=PASS done=exact lifecycle=present\r\n");
    return ERROR_SUCCESS;
}

static BOOL
A733ParseCommandLine(
    _Out_ A733_ARGUMENTS *Arguments)
{
    const WCHAR *CommandLine;
    SIZE_T Length;
    SIZE_T AllocationCount;
    PWSTR ReadCursor;
    PWSTR WriteCursor;

    A733ZeroBytes(Arguments, sizeof(*Arguments));
    CommandLine = GetCommandLineW();
    Length = 0;
    while (CommandLine[Length] != L'\0') {
        Length += 1;
    }
    AllocationCount = Length + 2U;
    Arguments->Storage = (PWSTR)HeapAlloc(
        GetProcessHeap(),
        0,
        AllocationCount * sizeof(WCHAR));
    Arguments->Values = (PWSTR *)HeapAlloc(
        GetProcessHeap(),
        0,
        AllocationCount * sizeof(PWSTR));
    if ((Arguments->Storage == NULL) || (Arguments->Values == NULL)) {
        if (Arguments->Storage != NULL) {
            (VOID)HeapFree(GetProcessHeap(), 0, Arguments->Storage);
        }
        if (Arguments->Values != NULL) {
            (VOID)HeapFree(GetProcessHeap(), 0, Arguments->Values);
        }
        A733ZeroBytes(Arguments, sizeof(*Arguments));
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    for (AllocationCount = 0; AllocationCount <= Length; AllocationCount += 1) {
        Arguments->Storage[AllocationCount] = CommandLine[AllocationCount];
    }

    ReadCursor = Arguments->Storage;
    WriteCursor = Arguments->Storage;
    while (*ReadCursor != L'\0') {
        BOOL InQuotes;

        while ((*ReadCursor == L' ') || (*ReadCursor == L'\t')) {
            ReadCursor += 1;
        }
        if (*ReadCursor == L'\0') {
            break;
        }
        Arguments->Values[Arguments->Count] = WriteCursor;
        Arguments->Count += 1;
        InQuotes = FALSE;
        for (;;) {
            DWORD Backslashes;
            DWORD Index;

            Backslashes = 0;
            while (*ReadCursor == L'\\') {
                Backslashes += 1;
                ReadCursor += 1;
            }
            if (*ReadCursor == L'"') {
                for (Index = 0; Index < (Backslashes / 2U); Index += 1) {
                    *WriteCursor = L'\\';
                    WriteCursor += 1;
                }
                if ((Backslashes & 1U) != 0) {
                    *WriteCursor = L'"';
                    WriteCursor += 1;
                    ReadCursor += 1;
                } else {
                    ReadCursor += 1;
                    if (InQuotes && (*ReadCursor == L'"')) {
                        *WriteCursor = L'"';
                        WriteCursor += 1;
                        ReadCursor += 1;
                    } else {
                        InQuotes = !InQuotes;
                    }
                }
                continue;
            }
            for (Index = 0; Index < Backslashes; Index += 1) {
                *WriteCursor = L'\\';
                WriteCursor += 1;
            }
            if ((*ReadCursor == L'\0') ||
                (!InQuotes && ((*ReadCursor == L' ') ||
                               (*ReadCursor == L'\t')))) {
                PWSTR NextArgument;

                NextArgument = ReadCursor;
                while ((*NextArgument == L' ') || (*NextArgument == L'\t')) {
                    NextArgument += 1;
                }
                *WriteCursor = L'\0';
                WriteCursor += 1;
                ReadCursor = NextArgument;
                break;
            }
            *WriteCursor = *ReadCursor;
            WriteCursor += 1;
            ReadCursor += 1;
        }
    }
    Arguments->Values[Arguments->Count] = NULL;
    return TRUE;
}

static VOID
A733FreeArguments(
    _Inout_ A733_ARGUMENTS *Arguments)
{
    if (Arguments->Values != NULL) {
        (VOID)HeapFree(GetProcessHeap(), 0, Arguments->Values);
    }
    if (Arguments->Storage != NULL) {
        (VOID)HeapFree(GetProcessHeap(), 0, Arguments->Storage);
    }
    A733ZeroBytes(Arguments, sizeof(*Arguments));
}

static VOID
A733WriteUsage(VOID)
{
    A733WriteConsoleText(
        STD_ERROR_HANDLE,
        "usage:\r\n"
        "  A733WinPeSmhcTrace.exe capture <log> <ready> [live|live-filtered]\r\n"
        "  A733WinPeSmhcTrace.exe stop\r\n"
        "  A733WinPeSmhcTrace.exe dump <log> [maxlines]\r\n"
        "  A733WinPeSmhcTrace.exe summary <log>\r\n"
        "  A733WinPeSmhcTrace.exe keysummary <log>\r\n"
        "  A733WinPeSmhcTrace.exe lowargsummary <log>\r\n"
        "  A733WinPeSmhcTrace.exe verify <log> <done>\r\n");
}

VOID WINAPI
A733SmhcTraceEntry(VOID)
{
    A733_ARGUMENTS Arguments;
    DWORD Result;
    ULONG MaxLines;

    if (!A733ParseCommandLine(&Arguments)) {
        Result = GetLastError();
        A733ReportError("parse-command-line", Result);
        ExitProcess(Result);
    }

    Result = ERROR_INVALID_PARAMETER;
    if (((Arguments.Count == 4) || (Arguments.Count == 5)) &&
        A733WideEquals(Arguments.Values[1], L"capture")) {
        BOOL LiveConsole;
        BOOL LiveInterestingOnly;
        BOOL ValidCaptureOption;

        LiveConsole = FALSE;
        LiveInterestingOnly = FALSE;
        ValidCaptureOption = TRUE;
        if ((Arguments.Count == 5) &&
            A733WideEquals(Arguments.Values[4], L"live")) {
            LiveConsole = TRUE;
        } else if ((Arguments.Count == 5) &&
                   A733WideEquals(Arguments.Values[4], L"live-filtered")) {
            LiveConsole = TRUE;
            LiveInterestingOnly = TRUE;
        } else if (Arguments.Count == 5) {
            A733ReportError(
                "invalid-capture-option",
                ERROR_INVALID_PARAMETER);
            ValidCaptureOption = FALSE;
        }
        if (ValidCaptureOption) {
            Result = A733Capture(
                Arguments.Values[2],
                Arguments.Values[3],
                LiveConsole,
                LiveInterestingOnly);
        }
    } else if ((Arguments.Count == 2) &&
               A733WideEquals(Arguments.Values[1], L"stop")) {
        Result = A733Stop();
    } else if (((Arguments.Count == 3) || (Arguments.Count == 4)) &&
               A733WideEquals(Arguments.Values[1], L"dump")) {
        MaxLines = A733_DEFAULT_DUMP_LINES;
        if ((Arguments.Count == 4) &&
            !A733ParsePositiveUlong(Arguments.Values[3], &MaxLines)) {
            A733ReportError("invalid-maxlines", ERROR_INVALID_PARAMETER);
        } else {
            Result = A733Dump(Arguments.Values[2], MaxLines);
        }
    } else if ((Arguments.Count == 3) &&
               A733WideEquals(Arguments.Values[1], L"summary")) {
        Result = A733Summary(Arguments.Values[2], FALSE, FALSE);
    } else if ((Arguments.Count == 3) &&
               A733WideEquals(Arguments.Values[1], L"keysummary")) {
        Result = A733Summary(Arguments.Values[2], TRUE, FALSE);
    } else if ((Arguments.Count == 3) &&
               A733WideEquals(Arguments.Values[1], L"lowargsummary")) {
        Result = A733Summary(Arguments.Values[2], FALSE, TRUE);
    } else if ((Arguments.Count == 4) &&
               A733WideEquals(Arguments.Values[1], L"verify")) {
        Result = A733Verify(Arguments.Values[2], Arguments.Values[3]);
    } else {
        A733WriteUsage();
    }

    A733FreeArguments(&Arguments);
    ExitProcess(Result);
}
