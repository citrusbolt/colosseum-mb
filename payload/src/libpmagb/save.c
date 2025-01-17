#include "global.h"
#include "gflib/types.h"
#include "libpmagb/save.h"
#include "libpmagb/agb_rom.h"
#include "gba/flash_internal.h"
#include "gflib/sound.h"
#include "gflib/init.h"

#define FILE_SIGNATURE 0x08012025  // signature value to determine if a sector is in use

//#define TOTAL_FLASH_SECTORS ((ARRAY_COUNT(sSaveBlockChunks) * 2) + (ARRAY_COUNT(sHallOfFameChunks) * 2)) // there are 2 slots, so double each array count and get the sum.
#define TOTAL_FLASH_SECTORS 32

struct SaveBlockChunk
{
    u8 *data;
    u16 size;
};

struct SaveSector
{
    u8 data[0xFF4];
    u16 id;
    u16 checksum;
    u32 signature;
    u32 counter;
}; // size is 0x1000

// Each 4 KiB flash sector contains 3968 bytes of actual data followed by a 128 byte footer
#define SECTOR_DATA_SIZE 3968u
#define SECTOR_FOOTER_SIZE 128

#define HALL_OF_FAME_SECTOR 28

#define NUM_SECTORS_PER_SAVE_SLOT 14  // Number of sectors occupied by a save slot
#define NUM_HALL_OF_FAME_SECTORS 2

#define SAVEBLOCK_CHUNK_EX(structure, size, chunkNum)         \
{                                                             \
    (u8 *)structure + chunkNum * SECTOR_DATA_SIZE,            \
    min(size - chunkNum * SECTOR_DATA_SIZE, SECTOR_DATA_SIZE) \
}                                                             \

#define SAVEBLOCK_CHUNK(structure, chunkNum) SAVEBLOCK_CHUNK_EX(&structure, sizeof(structure), chunkNum)

u16 gFirstSaveSector;
u32 gPrevSaveCounter;
u16 gLastKnownGoodSector;
u32 gDamagedSaveSectors;
u32 gSaveCounter;
struct SaveSector *gFastSaveSection; // the pointer is in fast IWRAM but may sometimes point to the slower EWRAM.
EWRAM_DATA struct SaveSector gSaveReadBuffer = {0};
u16 gUnknown_03005EB4;
u16 gSaveFileStatus;
u32 gGameContinueCallback;
u16 gUnknown_02023F40;
struct {
    u8 unk_0;
    u8 unk_1;
    u8 unk_2;
    u8 unk_3;
    u8 unk_4;
    u8 unk_5;
    u8 unk_6;
    u8 unk_7;
    u32 unk_8;
    u8 fill12;
    u8 fill13;
    u8 fill14;
    u8 fill15;
    u16 unk16;
} gUnknown_02022F10;

static u8 sWipeTries;
static u32 gSaveValidStatus;

bool8 WipeFailedSectors(void);

struct SaveBlockChunk sSaveBlockChunks[] = {
    {(u8 *)&gSaveBlock2Ptr, 0x00000004},
    {(u8 *)&gSaveBlock1Ptr + SECTOR_DATA_SIZE * 0, SECTOR_DATA_SIZE},
    {(u8 *)&gSaveBlock1Ptr + SECTOR_DATA_SIZE * 1, SECTOR_DATA_SIZE},
    {(u8 *)&gSaveBlock1Ptr + SECTOR_DATA_SIZE * 2, SECTOR_DATA_SIZE},
    {(u8 *)&gSaveBlock1Ptr + SECTOR_DATA_SIZE * 3, 0x0c40},
    {(u8 *)&gPokemonStoragePtr + SECTOR_DATA_SIZE * 0, SECTOR_DATA_SIZE},
    {(u8 *)&gPokemonStoragePtr + SECTOR_DATA_SIZE * 1, SECTOR_DATA_SIZE},
    {(u8 *)&gPokemonStoragePtr + SECTOR_DATA_SIZE * 2, SECTOR_DATA_SIZE},
    {(u8 *)&gPokemonStoragePtr + SECTOR_DATA_SIZE * 3, SECTOR_DATA_SIZE},
    {(u8 *)&gPokemonStoragePtr + SECTOR_DATA_SIZE * 4, SECTOR_DATA_SIZE},
    {(u8 *)&gPokemonStoragePtr + SECTOR_DATA_SIZE * 5, SECTOR_DATA_SIZE},
    {(u8 *)&gPokemonStoragePtr + SECTOR_DATA_SIZE * 6, SECTOR_DATA_SIZE},
    {(u8 *)&gPokemonStoragePtr + SECTOR_DATA_SIZE * 7, SECTOR_DATA_SIZE},
    {(u8 *)&gPokemonStoragePtr + SECTOR_DATA_SIZE * 8, 0x0004},
};

struct SaveBlockChunk gUnknown_020205BC[] = {
    {(void *)&gSaveReadBuffer, SECTOR_DATA_SIZE},
    {(void *)&gSaveReadBuffer, SECTOR_DATA_SIZE},
    {(void *)&gSaveReadBuffer, SECTOR_DATA_SIZE},
    {(void *)&gSaveReadBuffer, SECTOR_DATA_SIZE},
    {(void *)&gSaveReadBuffer, SECTOR_DATA_SIZE},
    {(void *)&gSaveReadBuffer, SECTOR_DATA_SIZE},
    {(void *)&gSaveReadBuffer, SECTOR_DATA_SIZE},
    {(void *)&gSaveReadBuffer, SECTOR_DATA_SIZE},
    {(void *)&gSaveReadBuffer, SECTOR_DATA_SIZE},
    {(void *)&gSaveReadBuffer, SECTOR_DATA_SIZE},
    {(void *)&gSaveReadBuffer, SECTOR_DATA_SIZE},
    {(void *)&gSaveReadBuffer, SECTOR_DATA_SIZE},
    {(void *)&gSaveReadBuffer, SECTOR_DATA_SIZE},
    {(void *)&gSaveReadBuffer, SECTOR_DATA_SIZE},
};

inline void sub_200A5C8_Inline(void)
{
    gSaveCounter = 0;
    gFirstSaveSector = 0;
    gDamagedSaveSectors = 0;
    gSaveValidStatus = 0x80;
}

inline void sub_200A5F0_Inline(void)
{
    s32 i;

    for (i = 0; i < NUM_SECTORS; i++)
    {
        EraseFlashSector(i);
    }
    sub_200A5C8_Inline();
}

s32 sub_020098D8(u32 timerNum, IntrFunc * intrFunc)
{
    sSaveBlockChunks[0].size = gAgbPmRomParams->saveBlock2Size;
    sSaveBlockChunks[4].size = gAgbPmRomParams->saveBlock1Size % SECTOR_DATA_SIZE;
    sSaveBlockChunks[13].size = sizeof(struct PokemonStorage) % SECTOR_DATA_SIZE;

    if (!IdentifyFlash() && !SetFlashTimerIntr(timerNum, intrFunc))
    {
        sub_200A5C8_Inline();
        return 0;
    }

    return -1;
}

void SetSaveSectorPtrs(void)
{
    sSaveBlockChunks[0].data = gSaveBlock2Ptr;
    sSaveBlockChunks[1].data = gSaveBlock1Ptr + 0 * SECTOR_DATA_SIZE;
    sSaveBlockChunks[2].data = gSaveBlock1Ptr + 1 * SECTOR_DATA_SIZE;
    sSaveBlockChunks[3].data = gSaveBlock1Ptr + 2 * SECTOR_DATA_SIZE;
    sSaveBlockChunks[4].data = gSaveBlock1Ptr + 3 * SECTOR_DATA_SIZE;
    sSaveBlockChunks[5].data = (void *)gPokemonStoragePtr + 0 * SECTOR_DATA_SIZE;
    sSaveBlockChunks[6].data = (void *)gPokemonStoragePtr + 1 * SECTOR_DATA_SIZE;
    sSaveBlockChunks[7].data = (void *)gPokemonStoragePtr + 2 * SECTOR_DATA_SIZE;
    sSaveBlockChunks[8].data = (void *)gPokemonStoragePtr + 3 * SECTOR_DATA_SIZE;
    sSaveBlockChunks[9].data = (void *)gPokemonStoragePtr + 4 * SECTOR_DATA_SIZE;
    sSaveBlockChunks[10].data = (void *)gPokemonStoragePtr + 5 * SECTOR_DATA_SIZE;
    sSaveBlockChunks[11].data = (void *)gPokemonStoragePtr + 6 * SECTOR_DATA_SIZE;
    sSaveBlockChunks[12].data = (void *)gPokemonStoragePtr + 7 * SECTOR_DATA_SIZE;
    sSaveBlockChunks[13].data = (void *)gPokemonStoragePtr + 8 * SECTOR_DATA_SIZE;
}

static inline u16 CalculateChecksum(void * data, s32 size)
{
    s32 i;
    u32 checksum = 0;

    for (i = 0; i < (size / 4); i++)
    {
        checksum += *((u32 *)data);
        data += sizeof(u32);
    }

    return ((checksum >> 16) + checksum);
}

static inline void DoReadFlashWholeSection(u8 sector, void * dest)
{
    ReadFlash(sector, 0, dest, sizeof(struct SaveSector));
}

enum
{
    SECTOR_DAMAGED,
    SECTOR_OK,
    SECTOR_CHECK, // unused
};

static inline bool32 SetSectorDamagedStatus(u8 op, u8 sectorNum)
{
    bool32 retVal = FALSE;

    switch (op)
    {
    case SECTOR_DAMAGED:
        gDamagedSaveSectors |= (1 << sectorNum);
        break;
    case SECTOR_OK:
        gDamagedSaveSectors &= ~(1 << sectorNum);
        break;
    case SECTOR_CHECK: // unused
        if (gDamagedSaveSectors & (1 << sectorNum))
            retVal = TRUE;
        break;
    }

    return retVal;
}

u16 GetSaveValidStatus(const struct SaveBlockChunk *chunks)
{
    u16 sector;
    bool8 signatureValid;
    u16 checksum;
    u32 slot1saveCounter = 0;
    u32 slot2saveCounter = 0;
    u8 slot1Status;
    u8 slot2Status;
    u32 validSectors;
    const u32 ALL_SECTORS = (1 << NUM_SECTORS_PER_SAVE_SLOT) - 1;  // bitmask of all saveblock sectors

    // check save slot 1.
    validSectors = 0;
    signatureValid = FALSE;
    for (sector = 0; sector < NUM_SECTORS_PER_SAVE_SLOT; sector++)
    {
        DoReadFlashWholeSection(sector, gFastSaveSection);
        if (gFastSaveSection->signature == FILE_SIGNATURE)
        {
            signatureValid = TRUE;
            checksum = CalculateChecksum(gFastSaveSection->data, chunks[gFastSaveSection->id].size);
            if (gFastSaveSection->checksum == checksum)
            {
                slot1saveCounter = gFastSaveSection->counter;
                validSectors |= 1 << gFastSaveSection->id;
            }
        }
    }

    if (signatureValid)
    {
        if (validSectors == ALL_SECTORS)
            slot1Status = SAVE_STATUS_OK;
        else
            slot1Status = SAVE_STATUS_ERROR;
    }
    else
    {
        slot1Status = SAVE_STATUS_EMPTY;
    }

    // check save slot 2.
    validSectors = 0;
    signatureValid = FALSE;
    for (sector = 0; sector < NUM_SECTORS_PER_SAVE_SLOT; sector++)
    {
        DoReadFlashWholeSection(NUM_SECTORS_PER_SAVE_SLOT + sector, gFastSaveSection);
        if (gFastSaveSection->signature == FILE_SIGNATURE)
        {
            signatureValid = TRUE;
            checksum = CalculateChecksum(gFastSaveSection->data, chunks[gFastSaveSection->id].size);
            if (gFastSaveSection->checksum == checksum)
            {
                slot2saveCounter = gFastSaveSection->counter;
                validSectors |= 1 << gFastSaveSection->id;
            }
        }
    }

    if (signatureValid)
    {
        if (validSectors == ALL_SECTORS)
            slot2Status = SAVE_STATUS_OK;
        else
            slot2Status = SAVE_STATUS_ERROR;
    }
    else
    {
        slot2Status = SAVE_STATUS_EMPTY;
    }

    if (slot1Status == SAVE_STATUS_OK && slot2Status == SAVE_STATUS_OK)
    {
        // Choose counter of the most recent save file
        if ((slot1saveCounter == -1 && slot2saveCounter == 0) || (slot1saveCounter == 0 && slot2saveCounter == -1))
        {
            if ((unsigned)(slot1saveCounter + 1) < (unsigned)(slot2saveCounter + 1))
                gSaveCounter = slot2saveCounter;
            else
                gSaveCounter = slot1saveCounter;
        }
        else
        {
            if (slot1saveCounter < slot2saveCounter)
                gSaveCounter = slot2saveCounter;
            else
                gSaveCounter = slot1saveCounter;
        }
        return SAVE_STATUS_OK;
    }

    if (slot1Status == SAVE_STATUS_OK)
    {
        gSaveCounter = slot1saveCounter;
        if (slot2Status == SAVE_STATUS_ERROR)
            return SAVE_STATUS_ERROR;
        else
            return SAVE_STATUS_OK;
    }

    if (slot2Status == SAVE_STATUS_OK)
    {
        gSaveCounter = slot2saveCounter;
        if (slot1Status == SAVE_STATUS_ERROR)
            return SAVE_STATUS_ERROR;
        else
            return SAVE_STATUS_OK;
    }

    if (slot1Status == SAVE_STATUS_EMPTY && slot2Status == SAVE_STATUS_EMPTY)
    {
        gSaveCounter = 0;
        gFirstSaveSector = 0;
        return SAVE_STATUS_EMPTY;
    }

    gSaveCounter = 0;
    gFirstSaveSector = 0;
    return 2;
}

void ReadSaveChunkI(u32 sector, u32 chunk, const struct SaveBlockChunk * chunks)
{
    s32 i;
    u32 checksum;
    u16 sectorId;
    DoReadFlashWholeSection(sector, gFastSaveSection);
    sectorId = gFastSaveSection->id;
    if (sectorId == 0)
    {
        gFirstSaveSector = chunk;
    }
    checksum = CalculateChecksum(gFastSaveSection->data, chunks[sectorId].size);
    if (gFastSaveSection->signature == FILE_SIGNATURE && gFastSaveSection->checksum == checksum)
    {
        for (i = 0; i < chunks[sectorId].size; i++)
            chunks[sectorId].data[i] = gFastSaveSection->data[i];
    }
}

inline bool32 sub_200A634_Inline(const struct SaveBlockChunk * chunks)
{
    s32 i;
    u32 firstSector = (gSaveCounter % 2) * NUM_SECTORS_PER_SAVE_SLOT;
    for (i = 0; i < NUM_SECTORS_PER_SAVE_SLOT; i++)
    {
        ReadSaveChunkI(i + firstSector, i, chunks);
    }
    return TRUE;
}

inline u32 sub_200A664_Inline(s32 a0, const struct SaveBlockChunk *chunks)
{
    gFastSaveSection = &gSaveReadBuffer;
    switch (a0)
    {
    case 0:
        gSaveValidStatus = GetSaveValidStatus(chunks);
        ReadSaveChunkI((gSaveCounter % 2) * NUM_SECTORS_PER_SAVE_SLOT, 0, chunks);
        return gSaveValidStatus;
    case 1:
        sub_200A634_Inline(chunks);
        return gSaveValidStatus;
    }
}

u32 ReadSaveBlockChunks(void)
{
    u32 status;
    const struct SaveBlockChunk * chunks = sSaveBlockChunks;
    gFastSaveSection = &gSaveReadBuffer;
    sub_200A634_Inline(chunks);
    status = gSaveValidStatus;
    CpuCopy16(gSaveBlock1Ptr, gSaveBlock1BakPtr, gAgbPmRomParams->saveBlock1Size);
    return status;
}

u8 * ReadFirstSaveSector(void)
{
    const struct SaveBlockChunk * chunks = gUnknown_020205BC;
    gFastSaveSection = &gSaveReadBuffer;
    gSaveValidStatus = GetSaveValidStatus(chunks);
    ReadSaveChunkI((gSaveCounter % 2) * NUM_SECTORS_PER_SAVE_SLOT, 0, chunks);
    return gSaveReadBuffer.data;
}

static inline u32 TryWriteSector(u8 sectorNum, void * data)
{
    u32 ret;

    SoundVSyncOff();
    ret = ProgramFlashSectorAndVerify(sectorNum, data);
    SoundVSyncOn();
    if (ret != 0)
    {
        SetSectorDamagedStatus(SECTOR_DAMAGED, sectorNum);
        return SAVE_STATUS_ERROR;
    }
    else
    {
        SetSectorDamagedStatus(SECTOR_OK, sectorNum);
        return SAVE_STATUS_OK;
    }
}

u32 WriteSingleChunk(u16 chunk, const struct SaveBlockChunk * chunks)
{
    u16 i;
    u16 r5;
    u8 * data;
    u16 size;

    r5 = gFirstSaveSector + chunk;
    r5 %= NUM_SECTORS_PER_SAVE_SLOT;
    r5 += (gSaveCounter % 2) * NUM_SECTORS_PER_SAVE_SLOT;
    data = chunks[chunk].data;
    size = chunks[chunk].size;

    for (i = 0; i < sizeof(struct SaveSector); i++)
    {
        ((u8 *)gFastSaveSection)[i] = 0;
    }
    gFastSaveSection->id = chunk;
    gFastSaveSection->signature = FILE_SIGNATURE;
    gFastSaveSection->counter = gSaveCounter;
    for (i = 0; i < size; i++)
    {
        gFastSaveSection->data[i] = data[i];
    }
    gFastSaveSection->checksum = CalculateChecksum(data, size);

    return TryWriteSector(r5, (void *)gFastSaveSection);
}

inline u32 WriteSaveBlockChunksInternal(u16 chunkId, const struct SaveBlockChunk * chunks)
{
    u32 retVal;
    s32 i;

    gFastSaveSection = &gSaveReadBuffer;

    if (chunkId != 0xFFFF)  // write single chunk
    {
        retVal = WriteSingleChunk(chunkId, chunks);
    }
    else  // write all chunks
    {
        gLastKnownGoodSector = gFirstSaveSector;
        gPrevSaveCounter = gSaveCounter;
        gFirstSaveSector++;
        gFirstSaveSector %= NUM_SECTORS_PER_SAVE_SLOT;
        gSaveCounter++;
        retVal = SAVE_STATUS_OK;

        for (i = 0; i < NUM_SECTORS_PER_SAVE_SLOT; i++)
            WriteSingleChunk(i, chunks);

        // Check for any bad sectors
        if (gDamagedSaveSectors != 0) // skip the damaged sector.
        {
            retVal = SAVE_STATUS_ERROR;
            gFirstSaveSector = gLastKnownGoodSector;
            gSaveCounter = gPrevSaveCounter;
        }
    }

    return retVal;
}

u32 WriteSaveBlockChunks(void)
{
    return WriteSaveBlockChunksInternal(0xFFFF, sSaveBlockChunks);
}

u8 sub_02009F4C(u16 chunk, const struct SaveBlockChunk * chunks)
{
    u16 i;
    u16 sectorId;
    u8 * data;
    u16 size;
    u8 ret;

    sectorId = chunk + gFirstSaveSector;
    sectorId %= NUM_SECTORS_PER_SAVE_SLOT;
    sectorId += (gSaveCounter % 2) * NUM_SECTORS_PER_SAVE_SLOT;
    data = chunks[chunk].data;
    size = chunks[chunk].size;

    for (i = 0; i < sizeof(struct SaveSector); i++)
    {
        ((u8 *)gFastSaveSection)[i] = 0;
    }

    gFastSaveSection->id = chunk;
    gFastSaveSection->signature = FILE_SIGNATURE;
    gFastSaveSection->counter = gSaveCounter;
    for (i = 0; i < size; i++)
    {
        gFastSaveSection->data[i] = data[i];
    }
    gFastSaveSection->checksum = CalculateChecksum(data, size);
    EraseFlashSector(sectorId);
    ret = SAVE_STATUS_OK;
    for (i = 0; i < 0xFF8; i++)
    {
        if (ProgramFlashByte(sectorId, i, gFastSaveSection->data[i]) != 0)
        {
            ret = SAVE_STATUS_ERROR;
            break;
        }
    }
    if (ret == SAVE_STATUS_ERROR)
    {
        SetSectorDamagedStatus(SECTOR_DAMAGED, sectorId);
        return SAVE_STATUS_ERROR;
    }
    else
    {
        ret = SAVE_STATUS_OK;
        for (i = 0; i < 7; i++)
        {
            if (ProgramFlashByte(sectorId, i + 0xFF9, ((u8 *)gFastSaveSection)[i + 0xFF9]) != 0)
            {
                ret = SAVE_STATUS_ERROR;
                break;
            }
        }
        if (ret == SAVE_STATUS_ERROR)
        {
            SetSectorDamagedStatus(SECTOR_DAMAGED, sectorId);
            return SAVE_STATUS_ERROR;
        }
        else
        {
            SetSectorDamagedStatus(SECTOR_OK, sectorId);
            return SAVE_STATUS_OK;
        }
    }
}

u8 sub_0200A118(u16 sectorNum, const struct SaveBlockChunk * chunks)
{
    u16 r4 = sectorNum + gFirstSaveSector - 1;
    r4 %= NUM_SECTORS_PER_SAVE_SLOT;
    r4 += (gSaveCounter % 2) * NUM_SECTORS_PER_SAVE_SLOT;

    SoundVSyncOff();
    if (ProgramFlashByte(r4, 0xFF8, ((u8 *)gFastSaveSection)[0xFF8]))
    {
        SetSectorDamagedStatus(SECTOR_DAMAGED, r4);
        SoundVSyncOn();
        return SAVE_STATUS_ERROR;
    }
    else
    {
        SetSectorDamagedStatus(SECTOR_OK, r4);
        SoundVSyncOn();
        return SAVE_STATUS_OK;
    }
}

u8 sub_0200A1B8(u16 sectorNum, const struct SaveBlockChunk * chunks)
{
    u16 r4 = sectorNum + gFirstSaveSector - 1;
    r4 %= NUM_SECTORS_PER_SAVE_SLOT;
    r4 += (gSaveCounter % 2) * NUM_SECTORS_PER_SAVE_SLOT;

    SoundVSyncOff();
    if (ProgramFlashByte(r4, 0xFF8, ~((u8 *)gFastSaveSection)[0xFF8]))
    {
        SetSectorDamagedStatus(SECTOR_DAMAGED, r4);
        SoundVSyncOn();
        return SAVE_STATUS_ERROR;
    }
    else
    {
        SetSectorDamagedStatus(SECTOR_OK, r4);
        SoundVSyncOn();
        return SAVE_STATUS_OK;
    }
}

inline bool32 sub_200A77C_Inline(void)
{
    gFastSaveSection = &gSaveReadBuffer;
    gLastKnownGoodSector = gFirstSaveSector;
    gPrevSaveCounter = gSaveCounter;
    gFirstSaveSector++;
    gFirstSaveSector %= NUM_SECTORS_PER_SAVE_SLOT;
    gSaveCounter++;
    gUnknown_02023F40 = 0;
    gDamagedSaveSectors = 0;
    return FALSE;
}

inline bool32 sub_200A7D8_Inline(void)
{
    gFastSaveSection = &gSaveReadBuffer;
    gLastKnownGoodSector = gFirstSaveSector;
    gPrevSaveCounter = gSaveCounter;
    gUnknown_02023F40 = 0;
    gDamagedSaveSectors = 0;
    return FALSE;
}

inline u32 sub_200A81C_Inline(void)
{
    gFirstSaveSector = gLastKnownGoodSector;
    gSaveCounter = gPrevSaveCounter;
    return gSaveCounter;
}

inline u8 sub_0200A260_sub(u16 limit, const struct SaveBlockChunk * chunks)
{
    u8 ret;

    if (gUnknown_02023F40 < limit - 1)
    {
        WriteSingleChunk(gUnknown_02023F40, chunks);
        if (gDamagedSaveSectors)
            ret = SAVE_STATUS_ERROR;
        else
        {
            ret = SAVE_STATUS_OK;
            gUnknown_02023F40++;
        }
    }
    else
    {
        ret = SAVE_STATUS_ERROR;
    }
    return ret;
}

inline u32 sub_200A880_Inline(u16 chunk, const struct SaveBlockChunk * chunks)
{
    u32 ret = 1;

    SoundVSyncOff();
    sub_02009F4C(chunk - 1, chunks);
    SoundVSyncOn();
    if (gDamagedSaveSectors != 0)
        ret = 0xFF;
    return ret;
}

bool32 sub_0200A260(void)
{
    u8 status;

retry:
    status = sub_0200A260_sub(NUM_SECTORS_PER_SAVE_SLOT, sSaveBlockChunks);
    if (gDamagedSaveSectors != 0)
    {
        if (!WipeFailedSectors())
        {
            goto retry;
        }
        gUnknown_02022F10.unk_4 |= 1;
    }
    if (status == SAVE_STATUS_ERROR)
        return TRUE;
    else
        return FALSE;
}

#ifdef NONMATCHING

bool32 sub_0200A2C8(s32 a)
{
    u8 i;

    switch (a)
    {
    case 0:
    {
        u32 i;
        s32 j;
        u8 *sav2;

        gUnknown_02022F10.unk_8 |= 1;
        gUnknown_02022F10.unk_4 &= ~(1);
        gUnknown_02022F10.unk_8 &= ~(2 | 4);
        sav2 = gSaveBlock2Ptr;
        for (i = 0; i < gAgbPmRomParams->saveBlock2Size; i++)
        {
            if (sav2[i] != 0)
                goto _0200A354;
        }
        sub_200A5F0_Inline();
        gUnknown_02022F10.unk_8 |= 8;
        gUnknown_02022F10.unk_1 = 4;
        gUnknown_02022F10.unk16 = 0;
    _0200A354:
        sub_200A77C_Inline();
        gUnknown_02022F10.unk_1++;
        gUnknown_02022F10.unk16 = 0;
        DelayFrames(5);
        while (1)
        {
            if (sub_0200A260() != 0)
                break;
            if (gUnknown_02022F10.unk_4 & 1)
                return TRUE;
        }
        sWipeTries = 0;
        while (1)
        {
            sub_200A880_Inline(14, sSaveBlockChunks);
            if (gDamagedSaveSectors != 0)
            {
                if (WipeFailedSectors())
                {
                    gUnknown_02022F10.unk_4 |= 1;
                    break;
                }
                continue;
            }
            break;
        }
        if (gUnknown_02022F10.unk_4 & 1)
            return TRUE;
        else
            return FALSE;
    }
    case 1:
        i = 0;
        while (1)
        {
            const struct SaveBlockChunk *chunks = sSaveBlockChunks;
            sub_0200A118(14, chunks);
            if (gDamagedSaveSectors != 0)
            {
                if (++i >= 4)
                {
                    gUnknown_02022F10.unk_4 |= 1;
                    break;
                }
                gDamagedSaveSectors = 0;
                continue;
            }
            break;
        }

        if (gUnknown_02022F10.unk_4 & 1)
            return TRUE;
        else
            return FALSE;
    case 2:
        i = 0;
        while (1)
        {
            const struct SaveBlockChunk *chunks = sSaveBlockChunks;
            sub_0200A1B8(14, chunks);
            if (gDamagedSaveSectors != 0)
            {
                if (++i >= 4)
                {
                    gUnknown_02022F10.unk_4 |= 1;
                    break;
                }
                gDamagedSaveSectors = 0;
                continue;
            }
            break;
        }
        return FALSE;
    }

    return FALSE;
}
#else

bool32 NAKED sub_0200A2C8(s32 a)
{
    asm_unified("push {r4, r5, lr}\t\n\
	cmp r0, #1\t\n\
	bne _0200A2D0\t\n\
	b _0200A42C\t\n\
_0200A2D0:\t\n\
	cmp r0, #1\t\n\
	bgt _0200A2DA\t\n\
	cmp r0, #0\t\n\
	beq _0200A2E2\t\n\
	b _0200A4AE\t\n\
_0200A2DA:\t\n\
	cmp r0, #2\t\n\
	bne _0200A2E0\t\n\
	b _0200A478\t\n\
_0200A2E0:\t\n\
	b _0200A4AE\t\n\
_0200A2E2:\t\n\
	ldr r2, =gUnknown_02022F10\t\n\
	ldr r1, [r2, #8]\t\n\
	movs r0, #1\t\n\
	orrs r1, r0\t\n\
	movs r0, #0xfe\t\n\
	ldrb r3, [r2, #4]\t\n\
	ands r0, r3\t\n\
	strb r0, [r2, #4]\t\n\
	movs r0, #7\t\n\
	rsbs r0, r0, #0\t\n\
	ands r1, r0\t\n\
	str r1, [r2, #8]\t\n\
	ldr r0, =gSaveBlock2Ptr\t\n\
	ldr r3, [r0]\t\n\
	movs r1, #0\t\n\
	ldr r0, =gAgbPmRomParams\t\n\
	ldr r0, [r0]\t\n\
	adds r0, #0x88\t\n\
	ldr r0, [r0]\t\n\
	cmp r1, r0\t\n\
	bhs _0200A31C\t\n\
	adds r2, r0, #0\t\n\
_0200A30E:\t\n\
	adds r0, r3, r1\t\n\
	ldrb r0, [r0]\t\n\
	cmp r0, #0\t\n\
	bne _0200A354\t\n\
	adds r1, #1\t\n\
	cmp r1, r2\t\n\
	blo _0200A30E\t\n\
_0200A31C:\t\n\
	movs r4, #0\t\n\
	ldr r5, =EraseFlashSector\t\n\
_0200A320:\t\n\
	lsls r0, r4, #0x10\t\n\
	lsrs r0, r0, #0x10\t\n\
	ldr r1, [r5]\t\n\
	bl _call_via_r1\t\n\
	adds r4, #1\t\n\
	cmp r4, #0x1f\t\n\
	ble _0200A320\t\n\
	ldr r0, =gSaveCounter\t\n\
	movs r3, #0\t\n\
	str r3, [r0]\t\n\
	ldr r0, =gFirstSaveSector\t\n\
	strh r3, [r0]\t\n\
	ldr r0, =gDamagedSaveSectors\t\n\
	str r3, [r0]\t\n\
	ldr r1, =gSaveValidStatus\t\n\
	movs r0, #0x80\t\n\
	str r0, [r1]\t\n\
	ldr r2, =gUnknown_02022F10\t\n\
	ldr r0, [r2, #8]\t\n\
	movs r1, #8\t\n\
	orrs r0, r1\t\n\
	str r0, [r2, #8]\t\n\
	movs r0, #4\t\n\
	strb r0, [r2, #1]\t\n\
	strh r3, [r2, #0x10]\t\n\
_0200A354:\t\n\
	ldr r1, =gFastSaveSection\t\n\
	ldr r0, =gSaveReadBuffer\t\n\
	str r0, [r1]\t\n\
	ldr r0, =gLastKnownGoodSector\t\n\
	ldr r4, =gFirstSaveSector\t\n\
	ldrh r1, [r4]\t\n\
	strh r1, [r0]\t\n\
	ldr r2, =gPrevSaveCounter\t\n\
	ldr r5, =gSaveCounter\t\n\
	ldr r0, [r5]\t\n\
	str r0, [r2]\t\n\
	adds r1, #1\t\n\
	strh r1, [r4]\t\n\
	ldrh r0, [r4]\t\n\
	movs r1, #0xe\t\n\
	bl __umodsi3\t\n\
	strh r0, [r4]\t\n\
	ldr r0, [r5]\t\n\
	adds r0, #1\t\n\
	str r0, [r5]\t\n\
	ldr r1, =gUnknown_02023F40\t\n\
	movs r0, #0\t\n\
	strh r0, [r1]\t\n\
	ldr r0, =gDamagedSaveSectors\t\n\
	movs r1, #0\t\n\
	str r1, [r0]\t\n\
	ldr r4, =gUnknown_02022F10\t\n\
	ldrb r0, [r4, #1]\t\n\
	adds r0, #1\t\n\
	strb r0, [r4, #1]\t\n\
	strh r1, [r4, #0x10]\t\n\
	movs r0, #5\t\n\
	bl DelayFrames\t\n\
	movs r5, #1\t\n\
	b _0200A3DE\t\n\
	.align 2, 0\t\n\
	.pool\t\n\
_0200A3D4:\t\n\
	adds r0, r5, #0\t\n\
	ldrb r1, [r4, #4]\t\n\
	ands r0, r1\t\n\
	cmp r0, #0\t\n\
	bne _0200A46E\t\n\
_0200A3DE:\t\n\
	bl sub_0200A260\t\n\
	cmp r0, #0\t\n\
	beq _0200A3D4\t\n\
	ldr r1, =sWipeTries\t\n\
	movs r0, #0\t\n\
	strb r0, [r1]\t\n\
_0200A3EC:\t\n\
	ldr r4, =sSaveBlockChunks\t\n\
	bl SoundVSyncOff\t\n\
	movs r0, #0xd\t\n\
	adds r1, r4, #0\t\n\
	bl sub_02009F4C\t\n\
	bl SoundVSyncOn\t\n\
	ldr r0, =gDamagedSaveSectors\t\n\
	ldr r0, [r0]\t\n\
	cmp r0, #0\t\n\
	beq _0200A462\t\n\
	bl WipeFailedSectors\t\n\
	lsls r0, r0, #0x18\t\n\
	cmp r0, #0\t\n\
	beq _0200A3EC\t\n\
	ldr r0, =gUnknown_02022F10\t\n\
	movs r1, #1\t\n\
	ldrb r2, [r0, #4]\t\n\
	orrs r1, r2\t\n\
	b _0200A460\t\n\
	.align 2, 0\t\n\
	.pool\t\n\
_0200A42C:\t\n\
	movs r4, #0\t\n\
_0200A42E:\t\n\
	ldr r1, =sSaveBlockChunks\t\n\
	movs r0, #0xe\t\n\
	bl sub_0200A118\t\n\
	ldr r1, =gDamagedSaveSectors\t\n\
	ldr r0, [r1]\t\n\
	cmp r0, #0\t\n\
	beq _0200A462\t\n\
	adds r0, r4, #1\t\n\
	lsls r0, r0, #0x18\t\n\
	lsrs r4, r0, #0x18\t\n\
	cmp r4, #3\t\n\
	bhi _0200A458\t\n\
	movs r0, #0\t\n\
	str r0, [r1]\t\n\
	b _0200A42E\t\n\
	.align 2, 0\t\n\
	.pool\t\n\
_0200A458:\t\n\
	ldr r0, =gUnknown_02022F10\t\n\
	movs r1, #1\t\n\
	ldrb r3, [r0, #4]\t\n\
	orrs r1, r3\t\n\
_0200A460:\t\n\
	strb r1, [r0, #4]\t\n\
_0200A462:\t\n\
	ldr r1, =gUnknown_02022F10\t\n\
	movs r0, #1\t\n\
	ldrb r1, [r1, #4]\t\n\
	ands r0, r1\t\n\
	cmp r0, #0\t\n\
	beq _0200A4AE\t\n\
_0200A46E:\t\n\
	movs r0, #1\t\n\
	b _0200A4B0\t\n\
	.align 2, 0\t\n\
	.pool\t\n\
_0200A478:\t\n\
	movs r4, #0\t\n\
_0200A47A:\t\n\
	ldr r1, =sSaveBlockChunks\t\n\
	movs r0, #0xe\t\n\
	bl sub_0200A1B8\t\n\
	ldr r1, =gDamagedSaveSectors\t\n\
	ldr r0, [r1]\t\n\
	cmp r0, #0\t\n\
	beq _0200A4AE\t\n\
	adds r0, r4, #1\t\n\
	lsls r0, r0, #0x18\t\n\
	lsrs r4, r0, #0x18\t\n\
	cmp r4, #3\t\n\
	bhi _0200A4A4\t\n\
	movs r0, #0\t\n\
	str r0, [r1]\t\n\
	b _0200A47A\t\n\
	.align 2, 0\t\n\
	.pool\t\n\
_0200A4A4:\t\n\
	ldr r0, =gUnknown_02022F10\t\n\
	movs r1, #1\t\n\
	ldrb r2, [r0, #4]\t\n\
	orrs r1, r2\t\n\
	strb r1, [r0, #4]\t\n\
_0200A4AE:\t\n\
	movs r0, #0\t\n\
_0200A4B0:\t\n\
	pop {r4, r5}\t\n\
	pop {r1}\t\n\
	bx r1\t\n\
	.align 2, 0\t\n\
	.pool");
}

#endif // NONMATCHING

static inline bool8 WipeSector_Sub(void)
{
    u16 i;

    u32 *ptr = (void *)&gSaveReadBuffer;
    for (i = 0; i < sizeof(struct SaveSector) / 4; i++, ptr++)
    {
        if (*ptr != 0)
            return TRUE;
    }
    return FALSE;
}

bool8 WipeSector(u16 sectorNum)
{
    u16 i;
    bool8 ret;
    u16 r5 = 0;

    SoundVSyncOff();
    while (r5 < 130)
    {
        for (i = 0; i < sizeof(struct SaveSector); i++)
        {
            ProgramFlashByte(sectorNum, i, 0);
        }
        ReadFlash(sectorNum, 0, &gSaveReadBuffer, sizeof(struct SaveSector));
        ret = WipeSector_Sub();
        r5++;
        if (!ret)
            break;
    }

    SoundVSyncOn();
    return ret;
}

bool8 WipeFailedSectors(void)
{
    u32 bits;
    u16 i;

    gUnknown_02022F10.unk_8 |= 2;
    if (gDamagedSaveSectors != 0 && sWipeTries <= 2)
    {
        bits = gDamagedSaveSectors;
        bits++;bits--; // Needed to match;

        for (i = 0; i < NUM_SECTORS; i++)
        {
            #ifdef NONMATCHING
            u32 currBits = 1 << i;
            #else
            register u32 currBits asm("r5") = 1 << i;
            #endif // NONMATCHING

            if (bits & (currBits))
            {
                if (!WipeSector(i))
                    bits &= ~(currBits);
            }
        }
        gDamagedSaveSectors = bits;
        sWipeTries++;
        if (bits == 0)
            return FALSE;
    }
    return TRUE;
}

static UNUSED void *sub_200A5C0(void)
{
    return &gSaveReadBuffer;
}
