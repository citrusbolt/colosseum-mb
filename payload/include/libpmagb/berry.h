#ifndef PMAGB_BERRY_H
#define PMAGB_BERRY_H

u32 GetEnigmaBerryChecksum(struct EnigmaBerry * enigmaBerry);
bool32 IsEnigmaBerryValid(void);
const struct Berry *GetBerryInfo(u8 berry);
const u8 *ItemId_GetName(u16 itemId);
void CopyItemName(u32 itemId, u8 *string, const u8 * berry_str);

#endif //PMAGB_BERRY_H
