#include "global.h"
#include "libpmagb.h"
#include "constants/berry.h"
#include "constants/items.h"

u32 GetEnigmaBerryChecksum(struct EnigmaBerry * enigmaBerry)
{
    u32 i, csum;
    u8 * dest;

    // the description pointers could be pointing to anywhere in memory. we do not want these
    // pointers to factor into the checksum as it will produce a different result every time: so
    // back the pointers up and set them to null so the checksum is safe to calculate.
    const u8 * desc1bak = gEnigmaBerryPtr->berry.description1;
    const u8 * desc2bak = gEnigmaBerryPtr->berry.description2;

    gEnigmaBerryPtr->berry.description1 = NULL;
    gEnigmaBerryPtr->berry.description2 = NULL;

    dest = (u8 *)enigmaBerry;
    csum = 0;
    for (i = 0; i < gAgbPmRomParams->enigmaBerrySize - 4; i++)
    {
        csum += dest[i];
    }

    // the checksum is calculated: the descriptions are safe to restore now.
    gEnigmaBerryPtr->berry.description1 = desc1bak;
    gEnigmaBerryPtr->berry.description2 = desc2bak;

    return csum;
}

// due to e-reader scans being particularly volatile to failure, it is a requirement to check for
// their integrity here due to scans possibly failing to produce the correct result.
bool32 IsEnigmaBerryValid(void)
{
    if (gEnigmaBerryPtr->berry.stageDuration == 0)
        return FALSE;
    if (gEnigmaBerryPtr->berry.maxYield == 0)
        return FALSE;
//    if (GetEnigmaBerryChecksum(gEnigmaBerryPtr) != gEnigmaBerryPtr->checksum)
//        return FALSE;
    return TRUE;
}

extern const struct Berry gBerries[];

const struct Berry *GetBerryInfo(u8 berry)
{
    // when getting the pointer to the berry info, enigma berries are handled differently. if your
    // berry is an Enigma Berry and its checksum is valid, fetch the pointer to its information in
    // the save block.
    if (berry == ITEM_TO_BERRY(ITEM_ENIGMA_BERRY) && IsEnigmaBerryValid())
        return &gEnigmaBerryPtr->berry;
    else
    {
        // invalid berries will be flattened into a cheri berry. Interestingly, if your berry was
        // an enigma berry whos checksum failed, the game will use the Enigma Berry information
        // for this: meaning if you see the Enigma Berry information, its actually because the
        // checksum failed.
        if (berry == BERRY_NONE || berry > ITEM_TO_BERRY(LAST_BERRY_INDEX))
            berry = ITEM_TO_BERRY(FIRST_BERRY_INDEX);
        return &gBerries[berry - 1];
    }
}
