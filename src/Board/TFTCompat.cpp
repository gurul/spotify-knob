/*-------------------------------------------------------------------------------------------------
** TFTCompat.cpp — see TFTCompat.h.
**
** The shared instance is deliberately NOT defined here. main.cpp owns it, as
** `TFT_eSPI tft = TFT_eSPI();`, exactly as the original source did.
** Defining it here too would be a duplicate symbol at link time.
** SPDX-License-Identifier: MIT
** ------------------------------------------------------------------------------------------------
*/

#include "TFTCompat.h"
