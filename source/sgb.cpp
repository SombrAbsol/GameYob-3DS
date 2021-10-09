#include <stdlib.h>
#include <string.h>

#include "gameboy.h"
#include "ppu.h"

#define sgbPalettes (vram[1])
#define sgbAttrFiles (vram[1]+0x1000)

void (Gameboy::*sgbCommands[])(int) = {
        &Gameboy::sgbPalXX, &Gameboy::sgbPalXX, &Gameboy::sgbPalXX, &Gameboy::sgbPalXX, &Gameboy::sgbAttrBlock,
        &Gameboy::sgbAttrLin, &Gameboy::sgbAttrDiv, &Gameboy::sgbAttrChr,
        &Gameboy::sgbSound, &Gameboy::sgbSouTrn, &Gameboy::sgbPalSet, &Gameboy::sgbPalTrn, &Gameboy::sgbAtrcEn,
        &Gameboy::sgbTestEn, &Gameboy::sgbIconEn, &Gameboy::sgbDataSnd,
        &Gameboy::sgbDataTrn, &Gameboy::sgbMltReq, &Gameboy::sgbJump, &Gameboy::sgbChrTrn, &Gameboy::sgbPctTrn,
        &Gameboy::sgbAttrTrn, &Gameboy::sgbAttrSet, &Gameboy::sgbMaskEn,
        &Gameboy::sgbObjTrn, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

void Gameboy::initSGB() {
    sgbPacketLength = 0;
    sgbNumControllers = 1;
    sgbSelectedController = 0;
    sgbPacketBit = -1;
    sgbPacketsTransferred = 0;
    sgbButtonsChecked = 0;

    memset(sgbMap, 0, 20 * 18);
}

void Gameboy::sgbHandleP1(u8 val) {
    if((val & 0x30) == 0) {
        // Start packet transfer
        sgbPacketBit = 0;
        ioRam[0x00] = 0xcf;
        return;
    }

    if(sgbPacketBit != -1) {
        u8 oldVal = ioRam[0x00];
        ioRam[0x00] = val;

        int shift = sgbPacketBit % 8;
        int byte = (sgbPacketBit / 8) % 16;
        if(shift == 0) {
            sgbPacket[byte] = 0;
        }

        int bit;
        if((oldVal & 0x30) == 0 && (val & 0x30) != 0x30) { // A bit of speculation here. Fixes castlevania.
            sgbPacketBit = -1;
            return;
        }

        if(!(val & 0x10)) {
            bit = 0;
        } else if(!(val & 0x20)) {
            bit = 1;
        } else {
            return;
        }

        sgbPacket[byte] |= bit << shift;
        sgbPacketBit++;
        if(sgbPacketBit == 128) {
            if(sgbPacketsTransferred == 0) {
                sgbCommand = sgbPacket[0] / 8;
                sgbPacketLength = sgbPacket[0] & 7;
            }

            if(sgbCommands[sgbCommand] != 0) {
                (this->*sgbCommands[sgbCommand])(sgbPacketsTransferred);
            }

            sgbPacketBit = -1;
            sgbPacketsTransferred++;
            if(sgbPacketsTransferred == sgbPacketLength) {
                sgbPacketLength = 0;
                sgbPacketsTransferred = 0;
            }
        }
    } else {
        if((val & 0x30) == 0x30) {
            if(sgbButtonsChecked == 3) {
                sgbSelectedController++;
                if(sgbSelectedController >= sgbNumControllers) {
                    sgbSelectedController = 0;
                }

                sgbButtonsChecked = 0;
            }

            ioRam[0x00] = 0xff - sgbSelectedController;
        } else {
            ioRam[0x00] = val | 0xcf;
            if((val & 0x30) == 0x10) {
                sgbButtonsChecked |= 1;
            } else if((val & 0x30) == 0x20) {
                sgbButtonsChecked |= 2;
            }
        }
    }
}

u8 Gameboy::sgbReadP1() {
    u8 p1 = ioRam[0x00];

    if(sgbMode && (p1 & 0x30) == 0x30) {
        return 0xff - sgbSelectedController;
    }

    if(!(p1 & 0x20)) {
        return 0xc0 | (p1 & 0xF0) | (controllers[sgbSelectedController] & 0xF);
    } else if(!(p1 & 0x10)) {
        return 0xc0 | (p1 & 0xF0) | ((controllers[sgbSelectedController] & 0xF0) >> 4);
    } else {
        return p1 | 0xcf;
    }
}

void Gameboy::setBackdrop(u16 val) {
    for(int i = 0; i < 4; i++) {
        bgPaletteData[i * 8] = val & 0xff;
        bgPaletteData[i * 8 + 1] = val >> 8;
        sprPaletteData[i * 8] = val & 0xff;
        sprPaletteData[i * 8 + 1] = val >> 8;
        sprPaletteData[(i + 4) * 8] = val & 0xff;
        sprPaletteData[(i + 4) * 8 + 1] = val >> 8;
    }

    for (int i = 0; i < 8; i++) {
        ppu->updateSprPaletteDMG(i);
    }

    ppu->updateBgPaletteDMG();
}

void Gameboy::sgbLoadAttrFile(int index) {
    if(index > 0x2c) {
        return;
    }

    int src = index * 90;
    int dest = 0;
    for(int i = 0; i < 20 * 18 / 4; i++) {
        sgbMap[dest++] = (sgbAttrFiles[src] >> 6) & 3;
        sgbMap[dest++] = (sgbAttrFiles[src] >> 4) & 3;
        sgbMap[dest++] = (sgbAttrFiles[src] >> 2) & 3;
        sgbMap[dest++] = (sgbAttrFiles[src] >> 0) & 3;
        src++;
    }
}

void Gameboy::sgbDoVramTransfer(u8* dest) {
    int map = 0x1800 + ((ioRam[0x40] >> 3) & 1) * 0x400;
    int index = 0;
    for(int y = 0; y < 18; y++) {
        for(int x = 0; x < 20; x++) {
            if(index == 0x1000) {
                return;
            }

            int tile = vram[0][map + y * 32 + x];
            if(ioRam[0x40] & 0x10) {
                memcpy(dest + index, vram[0] + tile * 16, 16);
            } else {
                memcpy(dest + index, vram[0] + 0x1000 + ((s8) tile) * 16, 16);
            }

            index += 16;
        }
    }
}

// Begin commands

void Gameboy::sgbPalXX(int block) {
    int s1, s2;
    switch(sgbCommand) {
        case 0:
            s1 = 0;
            s2 = 1;
            break;
        case 1:
            s1 = 2;
            s2 = 3;
            break;
        case 2:
            s1 = 0;
            s2 = 3;
            break;
        case 3:
            s1 = 1;
            s2 = 2;
            break;
        default:
            return;
    }

    memcpy(bgPaletteData + s1 * 8 + 2, sgbPacket + 3, 6);
    memcpy(sprPaletteData + s1 * 8 + 2, sgbPacket + 3, 6);
    memcpy(sprPaletteData + (s1+4) * 8 + 2, sgbPacket + 3, 6);

    memcpy(bgPaletteData + s2 * 8 + 2, sgbPacket + 9, 6);
    memcpy(sprPaletteData + s2 * 8 + 2, sgbPacket + 9, 6);
    memcpy(sprPaletteData + (s2+4) * 8 + 2, sgbPacket + 9, 6);

    setBackdrop(sgbPacket[1] | sgbPacket[2] << 8);
}

void Gameboy::sgbAttrBlock(int block) {
    int pos;
    if(block == 0) {
        sgbCmdData.attrBlock.dataBytes = 0;
        sgbCmdData.numDataSets = sgbPacket[1];
        pos = 2;
    } else {
        pos = 0;
    }

    u8* const data = sgbCmdData.attrBlock.data;
    while(pos < 16 && sgbCmdData.numDataSets > 0) {
        for(; sgbCmdData.attrBlock.dataBytes < 6 && pos < 16; sgbCmdData.attrBlock.dataBytes++, pos++) {
            data[sgbCmdData.attrBlock.dataBytes] = sgbPacket[pos];
        }

        if(sgbCmdData.attrBlock.dataBytes == 6) {
            int pIn = data[1] & 3;
            int pLine = (data[1] >> 2) & 3;
            int pOut = (data[1] >> 4) & 3;
            int x1 = data[2];
            int y1 = data[3];
            int x2 = data[4];
            int y2 = data[5];
            bool changeLine = data[0] & 2;
            if(!changeLine) {
                if((data[0] & 7) == 1) {
                    changeLine = true;
                    pLine = pIn;
                } else if((data[0] & 7) == 4) {
                    changeLine = true;
                    pLine = pOut;
                }
            }

            if(data[0] & 1) { // Inside block
                for(int x = x1 + 1; x < x2; x++) {
                    for(int y = y1 + 1; y < y2; y++) {
                        sgbMap[y * 20 + x] = pIn;
                    }
                }
            }

            if(data[0] & 4) { // Outside block
                for(int x = 0; x < 20; x++) {
                    if(x < x1 || x > x2) {
                        for(int y = 0; y < 18; y++) {
                            if(y < y1 || y > y2) {
                                sgbMap[y * 20 + x] = pOut;
                            }
                        }
                    }
                }
            }

            if(changeLine) { // Line surrounding block
                for(int x = x1; x <= x2; x++) {
                    sgbMap[y1 * 20 + x] = pLine;
                    sgbMap[y2 * 20 + x] = pLine;
                }

                for(int y = y1; y <= y2; y++) {
                    sgbMap[y * 20 + x1] = pLine;
                    sgbMap[y * 20 + x2] = pLine;
                }
            }

            sgbCmdData.attrBlock.dataBytes = 0;
            sgbCmdData.numDataSets--;
        }
    }
}

void Gameboy::sgbAttrLin(int block) {
    int index = 0;
    if(block == 0) {
        sgbCmdData.numDataSets = sgbPacket[1];
        index = 2;
    }

    while(sgbCmdData.numDataSets > 0 && index < 16) {
        u8 dat = sgbPacket[index++];
        sgbCmdData.numDataSets--;

        int line = dat & 0x1f;
        int pal = (dat >> 5) & 3;

        if(dat & 0x80) { // Horizontal
            for(int i = 0; i < 20; i++) {
                sgbMap[i + line * 20] = pal;
            }
        } else { // Vertical
            for(int i = 0; i < 18; i++) {
                sgbMap[line + i * 20] = pal;
            }
        }
    }
}

void Gameboy::sgbAttrDiv(int block) {
    int p0 = (sgbPacket[1] >> 2) & 3;
    int p1 = (sgbPacket[1] >> 4) & 3;
    int p2 = (sgbPacket[1] >> 0) & 3;

    if(sgbPacket[1] & 0x40) {
        for(int y = 0; y < sgbPacket[2] && y < 18; y++) {
            for(int x = 0; x < 20; x++) {
                sgbMap[y * 20 + x] = p0;
            }
        }

        if(sgbPacket[2] < 18) {
            for(int x = 0; x < 20; x++) {
                sgbMap[sgbPacket[2] * 20 + x] = p1;
            }

            for(int y = sgbPacket[2] + 1; y < 18; y++) {
                for(int x = 0; x < 20; x++) {
                    sgbMap[y * 20 + x] = p2;
                }
            }
        }
    } else {
        for(int x = 0; x < sgbPacket[2] && x < 20; x++) {
            for(int y = 0; y < 18; y++) {
                sgbMap[y * 20 + x] = p0;
            }
        }

        if(sgbPacket[2] < 20) {
            for(int y = 0; y < 18; y++) {
                sgbMap[y * 20 + sgbPacket[2]] = p1;
            }

            for(int x = sgbPacket[2] + 1; x < 20; x++) {
                for(int y = 0; y < 18; y++) {
                    sgbMap[y * 20 + x] = p2;
                }
            }
        }
    }
}

void Gameboy::sgbAttrChr(int block) {
    u8 &x = sgbCmdData.attrChr.x;
    u8 &y = sgbCmdData.attrChr.y;

    int index = 0;
    if(block == 0) {
        sgbCmdData.numDataSets = sgbPacket[3] | (sgbPacket[4] << 8);
        sgbCmdData.attrChr.writeStyle = sgbPacket[5] & 1;
        x = (sgbPacket[1] >= 20 ? 19 : sgbPacket[1]);
        y = (sgbPacket[2] >= 18 ? 17 : sgbPacket[2]);

        index = 6 * 4;
    }

    while(sgbCmdData.numDataSets != 0 && index < 16 * 4) {
        sgbMap[x + y * 20] = (sgbPacket[index / 4] >> (6 - (index & 3) * 2)) & 3;
        if(sgbCmdData.attrChr.writeStyle == 0) {
            x++;
            if(x == 20) {
                x = 0;
                y++;
                if(y == 18) {
                    y = 0;
                }
            }
        } else {
            y++;
            if(y == 18) {
                y = 0;
                x++;
                if(x == 20) {
                    x = 0;
                }
            }
        }

        index++;
        sgbCmdData.numDataSets--;
    }
}

void Gameboy::sgbSound(int block) {
    // TODO
}

void Gameboy::sgbSouTrn(int block) {
    // TODO
}

void Gameboy::sgbPalSet(int block) {
    for(int i = 0; i < 4; i++) {
        int paletteid = (sgbPacket[i * 2 + 1] | (sgbPacket[i * 2 + 2] << 8)) & 0x1ff;
        memcpy(bgPaletteData + i * 8 + 2, sgbPalettes + paletteid * 8 + 2, 6);
        memcpy(sprPaletteData + i * 8 + 2, sgbPalettes + paletteid * 8 + 2, 6);
        memcpy(sprPaletteData + (i+4) * 8 + 2, sgbPalettes + paletteid * 8 + 2, 6);
    }

    int color0Paletteid = (sgbPacket[1] | (sgbPacket[2] << 8)) & 0x1ff;
    setBackdrop(sgbPalettes[color0Paletteid * 8] | (sgbPalettes[color0Paletteid * 8 + 1] << 8));

    if(sgbPacket[9] & 0x80) {
        sgbLoadAttrFile(sgbPacket[9] & 0x3f);
    }

    if(sgbPacket[9] & 0x40) {
        ppu->setSgbMask(0);
    }
}

void Gameboy::sgbPalTrn(int block) {
    sgbDoVramTransfer(sgbPalettes);
}

void Gameboy::sgbAtrcEn(int block) {
    // TODO
}

void Gameboy::sgbTestEn(int block) {
    // TODO
}

void Gameboy::sgbIconEn(int block) {
    // TODO
}

void Gameboy::sgbDataSnd(int block) {
    // TODO
}

void Gameboy::sgbDataTrn(int block) {
    // TODO
}

void Gameboy::sgbMltReq(int block) {
    sgbNumControllers = (sgbPacket[1] & 3) + 1;
    sgbSelectedController = sgbNumControllers > 1 ? 1 : 0;
}

void Gameboy::sgbJump(int block) {
    // TODO
}

void Gameboy::sgbChrTrn(int block) {
    u8* data = (u8*) malloc(0x1000);
    sgbDoVramTransfer(data);
    ppu->setSgbTiles(data, sgbPacket[1]);
    free(data);
}

void Gameboy::sgbPctTrn(int block) {
    u8* data = (u8*) malloc(0x1000);
    sgbDoVramTransfer(data);
    ppu->setSgbMap(data);
    free(data);
}

void Gameboy::sgbAttrTrn(int block) {
    sgbDoVramTransfer(sgbAttrFiles);
}

void Gameboy::sgbAttrSet(int block) {
    sgbLoadAttrFile(sgbPacket[1] & 0x3f);
    if(sgbPacket[1] & 0x40) {
        ppu->setSgbMask(0);
    }
}

void Gameboy::sgbMaskEn(int block) {
    ppu->setSgbMask(sgbPacket[1] & 3);
}

void Gameboy::sgbObjTrn(int block) {
    // TODO
}

// End commands
