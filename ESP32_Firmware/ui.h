// =============================================================================
//  ui.h — LCD 20x4 + Keypad 4x4. Render dari snapshot, input → cmdSend().
//  UI nav state TERPISAH dari operating state. Lihat ARCHITECTURE.md §7.
// =============================================================================
#ifndef UI_H
#define UI_H

void uiInit();
void uiTick();

#endif // UI_H
