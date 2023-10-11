/*
                        Milano Smart Park Firmware
                       Developed by Norman Mulinacci

          This code is usable under the terms and conditions of the
             GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007
*/

// Firmware Icons

#ifndef ICONS_H
#define ICONS_H

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

static unsigned char msp_icon64x64[] = { // Milano Smart Park project logo
  0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
  0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xFF, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x80, 0xFF, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xFF, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFC, 0x1F, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0xE0, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x80, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0xFC, 0x03, 0x00, 0x00, 0x00, 0x00, 0x80,
  0x07, 0xF0, 0x03, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x1F, 0xE0, 0x0F, 0x00,
  0x00, 0x00, 0x00, 0x80, 0x7F, 0xC0, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x80,
  0xFF, 0x81, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x03, 0x3F, 0x00,
  0x00, 0x00, 0x00, 0x00, 0xFC, 0x07, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xE0, 0x1F, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x1F, 0xF8, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0xF0, 0x01, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x7E, 0xF0, 0x03, 0x00, 0x00, 0x00, 0x80, 0x07, 0xFC, 0xE0, 0x03,
  0x00, 0x00, 0x00, 0xC0, 0x1F, 0xF8, 0xC1, 0x07, 0x00, 0x00, 0x00, 0xC0,
  0x7F, 0xF0, 0xC3, 0x07, 0x00, 0x00, 0x00, 0x80, 0x7F, 0xE0, 0x83, 0x0F,
  0x00, 0x00, 0x00, 0x00, 0xFF, 0xC1, 0x87, 0x0F, 0x00, 0x00, 0x3E, 0x00,
  0xFC, 0xC1, 0x07, 0x1F, 0x00, 0x80, 0xFF, 0x00, 0xF0, 0x83, 0x0F, 0x1F,
  0x00, 0xC0, 0xFF, 0x01, 0xE0, 0x07, 0x0F, 0x1E, 0x00, 0xE0, 0xFF, 0x01,
  0xC0, 0x07, 0x1F, 0x3E, 0x00, 0xFF, 0xFF, 0x7F, 0x80, 0x0F, 0x1E, 0x3C,
  0xC0, 0xFF, 0xFF, 0xFF, 0x00, 0x1F, 0x3E, 0x7C, 0xE0, 0xFF, 0xFF, 0xFF,
  0x03, 0x1F, 0x3E, 0x7C, 0xF0, 0xFF, 0xFF, 0xFF, 0x03, 0x1E, 0x3E, 0x7C,
  0xF0, 0xFF, 0xFF, 0xFF, 0x07, 0x3E, 0x7C, 0x78, 0xF8, 0xFF, 0xFF, 0xFF,
  0x07, 0x3E, 0x7C, 0xF8, 0xF8, 0xFF, 0xFF, 0xFF, 0x07, 0x3E, 0x7C, 0xF8,
  0xF8, 0xFF, 0xFF, 0xFF, 0x0F, 0x3E, 0x7C, 0xF8, 0xF8, 0xFF, 0xFF, 0xFF,
  0x07, 0x7E, 0x7C, 0xF8, 0xF8, 0xFF, 0xFF, 0xFF, 0x07, 0x3E, 0x7C, 0xF8,
  0xFC, 0xFF, 0xFF, 0xFF, 0x1F, 0x3E, 0x78, 0xF0, 0xFE, 0xFF, 0xFF, 0xFF,
  0x1F, 0x3E, 0xFC, 0xF8, 0xFE, 0xFF, 0xFF, 0xFF, 0x3F, 0x3C, 0x78, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0x3F, 0x00, 0x7C, 0xF8, 0xFF, 0xFF, 0xFF, 0xFF,
  0x7F, 0x00, 0x7C, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x00, 0x7C, 0xF0,
  0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x00, 0x78, 0xF8, 0xFF, 0xFF, 0xFF, 0xFF,
  0x3F, 0x00, 0x10, 0xF8, 0xFE, 0xFF, 0xFF, 0xFF, 0x3F, 0x00, 0x00, 0xF8,
  0xFE, 0xFF, 0xFF, 0xFF, 0x1F, 0x00, 0x00, 0xF8, 0xFC, 0xFF, 0xFF, 0xFF,
  0x1F, 0x00, 0x00, 0xF0, 0xF8, 0xFF, 0xFF, 0xFF, 0x0F, 0x00, 0x00, 0x20,
  0xF8, 0xFF, 0xFF, 0xFF, 0x07, 0x00, 0x00, 0x00, 0xF8, 0xFF, 0xFF, 0xFF,
  0x07, 0x00, 0x00, 0x00, 0xF8, 0xFF, 0xFF, 0xFF, 0x07, 0x00, 0x00, 0x00,
  0xF8, 0xFF, 0xFF, 0xFF, 0x07, 0x00, 0x00, 0x00, 0xF0, 0xFF, 0xFF, 0xFF,
  0x07, 0x00, 0x00, 0x00, 0xF0, 0xFF, 0xFF, 0xFF, 0x07, 0x00, 0x00, 0x00,
  0xE0, 0xFF, 0xFF, 0xFF, 0x03, 0x00, 0x00, 0x00, 0xE0, 0xFF, 0xFF, 0xFF,
  0x01, 0x00, 0x00, 0x00, 0xC0, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
  0x00, 0xFE, 0xFF, 0x5F, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0xFF, 0x01,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00,
};

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

unsigned char wifi1_icon16x16[] = {
  0b00000000, 0b00000000, //
  0b11100000, 0b00000111, //      ######
  0b11111000, 0b00011111, //    ##########
  0b11111100, 0b00111111, //   ############
  0b00001110, 0b01110000, //  ###        ###
  0b11100110, 0b01100111, //  ##  ######  ##
  0b11110000, 0b00001111, //     ########
  0b00011000, 0b00011000, //    ##      ##
  0b11000000, 0b00000011, //       ####
  0b11100000, 0b00000111, //      ######
  0b00100000, 0b00000100, //      #    #
  0b10000000, 0b00000001, //        ##
  0b10000000, 0b00000001, //        ##
  0b00000000, 0b00000000, //
  0b00000000, 0b00000000, //
  0b00000000, 0b00000000, //
};

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

unsigned char mobile_icon16x16[] = {
  0b00000000, 0b00000000, //                
  0b00000000, 0b01100000, //             ## 
  0b00000000, 0b01100000, //             ## 
  0b00000000, 0b01100000, //             ## 
  0b00000000, 0b01100000, //             ## 
  0b00000000, 0b01100110, //         ##  ## 
  0b00000000, 0b01100110, //         ##  ## 
  0b00000000, 0b01100110, //         ##  ## 
  0b00000000, 0b01100110, //         ##  ## 
  0b01100000, 0b01100110, //     ##  ##  ## 
  0b01100000, 0b01100110, //     ##  ##  ## 
  0b01100000, 0b01100110, //     ##  ##  ## 
  0b01100000, 0b01100110, //     ##  ##  ## 
  0b01100110, 0b01100110, // ##  ##  ##  ## 
  0b01100110, 0b01100110, // ##  ##  ##  ## 
  0b00000000, 0b00000000, //                
};

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

unsigned char blank_icon16x16[] = {
  0b00000000, 0b00000000, //
  0b00000000, 0b00000000, //
  0b00000000, 0b00000000, //
  0b00000000, 0b00000000, //
  0b00000000, 0b00000000, //
  0b00000000, 0b00000000, //
  0b00000000, 0b00000000, //
  0b00000000, 0b00000000, //
  0b00000000, 0b00000000, //
  0b00000000, 0b00000000, //
  0b00000000, 0b00000000, //
  0b00000000, 0b00000000, //
  0b00000000, 0b00000000, //
  0b00000000, 0b00000000, //
  0b00000000, 0b00000000, //
  0b00000000, 0b00000000, //
};

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

unsigned char nocon_icon16x16[] = {
  0b00000000, 0b00000000, //
  0b11100000, 0b00000011, //       #####
  0b11111000, 0b00001111, //     #########
  0b11111100, 0b00011111, //    ###########
  0b00111110, 0b00111110, //   #####   #####
  0b01111110, 0b00111000, //   ###    ######
  0b11111111, 0b01110000, //  ###    ########
  0b11110111, 0b01110001, //  ###   ##### ###
  0b11000111, 0b01110011, //  ###  ####   ###
  0b10000111, 0b01110111, //  ### ####    ###
  0b00001110, 0b00111111, //   ######    ###
  0b00011110, 0b00111110, //   #####    ####
  0b11111100, 0b00011111, //    ###########
  0b11111000, 0b00001111, //     #########
  0b11100000, 0b00000011, //       #####
  0b00000000, 0b00000000, //
};

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

unsigned char sd_icon16x16[] = {
  0b00000000, 0b00000000, //
  0b00000000, 0b00000000, //
  0b11111110, 0b01111111, //  ##############
  0b11111111, 0b11111111, // ################
  0b11111111, 0b11000011, // ##    ##########
  0b11111111, 0b11111111, // ################
  0b11111111, 0b11000011, // ##    ##########
  0b11111111, 0b11111111, // ################
  0b11111111, 0b11000011, // ##    ##########
  0b11111111, 0b11111111, // ################
  0b11111111, 0b11000011, // ##    ##########
  0b11111111, 0b11111111, // ################
  0b11111111, 0b01111111, //  ###############
  0b00111111, 0b00000111, //      ###  ######
  0b00011110, 0b00000011, //       ##   ####
  0b00000000, 0b00000000, //
};

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

unsigned char clock_icon16x16[] = {
  0b00000000, 0b00000000, //
  0b00000000, 0b00000000, //
  0b11100000, 0b00000011, //       #####
  0b11110000, 0b00000111, //      #######
  0b00011000, 0b00001100, //     ##     ##
  0b00001100, 0b00011000, //    ##       ##
  0b00000110, 0b00110000, //   ##         ##
  0b00000110, 0b00110000, //   ##         ##
  0b11111110, 0b00110000, //   ##    #######
  0b10000110, 0b00110000, //   ##    #    ##
  0b10000110, 0b00110000, //   ##    #    ##
  0b10001100, 0b00011000, //    ##   #   ##
  0b00011000, 0b00001100, //     ##     ##
  0b11110000, 0b00000111, //      #######
  0b11100000, 0b00000011, //       #####
  0b00000000, 0b00000000, //
};

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

#endif