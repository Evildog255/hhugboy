/*
   hhugboy Game Boy emulator
   copyright 2013 taizou

   Based on GEST
   Copyright (C) 2003-2010 TM
   This file incorporates code from VisualBoyAdvance
   Copyright (C) 1999-2004 by Forgotten

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#define UNICODE

#include <stdio.h>
#include <ddraw.h>

#include <iostream>
#include <fstream>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <string>
#include <math.h>
using namespace std;

#include "directdraw.h"
#include "GB.h"
#include "scale2x.h"
#include "scale3x.h"
#include "cpu.h"
#include "debug.h"
#include "SGB.h"
#include "strings.h"
#include "render.h"
#include "config.h"

#include "main.h"

int RGB_BIT_MASK = 0;

DWORD* gfx_pal32 = NULL;
WORD* gfx_pal16 = NULL;

void (DirectDraw::*drawScreen)() = NULL; 

bool initPalettes()
{
    
    if(renderer.bitCount  == 16) {
        gfx_pal16 = new WORD[0x10000];
    } else {
        gfx_pal32 = new DWORD[0x10000];
    }
    
    mix_gbc_colors();
  
    if(!gfx_pal32 && !gfx_pal16) {
        debug_print(str_table[ERROR_MEMORY]); 
        return false;
    }
    
    return true;
}


void mix_gbc_colors()
{
  if(GB1->gbc_mode && options->video_GBCBGA_real_colors)
  {
     if(GB1->system_type == SYS_GBA)
     {
        for(int i=0;i<0x10000;++i)
        {
           int red_init = (i & 0x1F);
           int green_init = ((i & 0x3E0) >> 5);
           int blue_init = ((i & 0x7C00) >> 10);
         
           if(red_init < 0x19) red_init -= 4; else red_init -= 3; 
           if(green_init < 0x19) green_init -= 4; else green_init -= 3;
           if(blue_init < 0x19) blue_init -= 4; else blue_init -= 3;
           if(red_init < 0) red_init = 0;       
           if(green_init < 0) green_init = 0;
           if(blue_init < 0) blue_init = 0;
        
           int red = ((red_init*12+green_init+blue_init)/14);
           int green = ((green_init*12+blue_init+red_init)/14);
           int blue = ((blue_init*12+red_init+green_init)/14);
           if(renderer.bitCount == 16)
              gfx_pal16[i] = (red<<renderer.rs) | (green<<renderer.gs) | (blue<<renderer.bs);              
           else
              gfx_pal32[i] = (red<<renderer.rs) | (green<<renderer.gs) | (blue<<renderer.bs);
        }
     }     
     else
     {
        for(int i=0;i<0x10000;++i)
        {
           int red_init = (i & 0x1F);
           int green_init = ((i & 0x3E0) >> 5);
           int blue_init = ((i & 0x7C00) >> 10);
         
           if(red_init && red_init < 0x10) red_init += 2; else if(red_init) red_init += 3; 
           if(green_init && green_init < 0x10) green_init += 2; else if(green_init) green_init += 3;
           if(blue_init && blue_init < 0x10) blue_init += 2; else if(blue_init) blue_init += 3;
           if(red_init >= 0x1F) red_init = 0x1E;       
           if(green_init >= 0x1F) green_init = 0x1E;
           if(blue_init >= 0x1F) blue_init = 0x1E;
        
           int red = ((red_init*10+green_init*3+blue_init)/14);
           int green = ((green_init*10+blue_init*2+red_init*2)/14);
           int blue = ((blue_init*10+red_init*2+green_init*2)/14);
           if(renderer.bitCount == 16)
              gfx_pal16[i] = (red<<renderer.rs) | (green<<renderer.gs) | (blue<<renderer.bs);
           else        
              gfx_pal32[i] = (red<<renderer.rs) | (green<<renderer.gs) | (blue<<renderer.bs);
        }
     }
  } else
  {
     if(renderer.bitCount == 16)
     {
        for(int i=0;i<0x10000;++i)
           gfx_pal16[i] = ((i & 0x1F) << renderer.rs) | (((i & 0x3E0) >> 5) << renderer.gs) | (((i & 0x7C00) >> 10) << renderer.bs);
     } else
     {
        for(int i=0;i<0x10000;++i)
            gfx_pal32[i] = ((i & 0x1F) << renderer.rs) | (((i & 0x3E0) >> 5) << renderer.gs) | (((i & 0x7C00) >> 10) << renderer.bs);
           //gfx_pal32[i] = (((i & 0x1F) << rs) | (((i & 0x3E0) >> 5) << gs) | (((i & 0x7C00) >> 10) << bs)) ^ 0xFFFFFFFF; = negative
           //gfx_pal32[i] = ((i & 0x1F) << rs) | (((i & 0x3E0) >> 5) << gs) | (((i & 0x7C00) >> 10) << bs) ^ 0xFFFFFFFF; = super yellow ridiculousness. i actually quite enjoy this
     }  
  }
}

void filter_none_32(DWORD *pointer,DWORD *source,int width,int height,int pitch)
{
   copy_line32(pointer,source,width*height); 
}

void filter_none_16(WORD *pointer,WORD *source,int width,int height,int pitch)
{
   copy_line16(pointer,source,width*height);
}

void softwarexx_16(WORD *pointer,WORD *source,int width,int height,int pitch)
{
   register WORD *target;
   WORD* init = source;
   
	// renderer.gameboyFilterHeight indicates scale 
   for(register int y = 0;y < height*renderer.gameboyFilterHeight;y++)
   { 
      target = pointer + y*pitch;
      source = init + (y/renderer.gameboyFilterHeight)*width;
      for(int x = 0;x < width; x++)
      {
      	 for (int s = 0; s < renderer.gameboyFilterHeight - 1; s++) {
      	 	*target++ = *source;
      	 }
         *target++ = *source++;
      }
  }
}

void softwarexx_32(DWORD *pointer,DWORD *source,int width,int height,int pitch)
{
   register DWORD *target;
   DWORD* init = source;

	// renderer.gameboyFilterHeight indicates scale 
   for(register int y = 0;y < height*renderer.gameboyFilterHeight;y++)
   { 
      target = pointer + y*pitch;
      source = init + (y/renderer.gameboyFilterHeight)*width;
      for(int x = 0;x < width; x++)
      {
      	 for (int s = 0; s < renderer.gameboyFilterHeight - 1; s++) {
      	 	*target++ = *source;
      	 }
         *target++ = *source++;
      }
  }
}

/*
void blur_32(DWORD *pointer,DWORD *source,int width,int height,int pitch)
{
   register DWORD *target = pointer;
   DWORD* init = source;

   *target++ = (((*(source+width)) + ((*source)<<1) + *(source+1))>>2);
   *target++ = (((*(source+width)) + ((*source)<<1) + *(source+1))>>2);
   *source++;    
   for(int x=1;x<width-1;x++)
   {
      *target++ = (((*(source+width)) + ((*source)<<1) + *(source-1))>>2);
      *target++ = (((*(source+width)) + ((*source)<<1) + *(source+1))>>2);
      *source++;                                    
   } 
   *target++ = (((*(source+width)) + ((*source)<<1) + *(source-1))>>2);
   *target++ = (((*(source+width)) + ((*source)<<1) + *(source-1))>>2);
   *source++;     
      
   target = pointer+pitch;      
   source=init; 
   
   *target++ = (((*(source+width)) + ((*source)<<1) + *(source+1))>>2);
   *target++ = (((*(source+width)) + ((*source)<<1) + *(source+1))>>2);
   *source++;     
   for(int x=1;x<width-1;x++)
   {
      *target++ = (((*(source+width)) + ((*source)<<1) + *(source-1))>>2);
      *target++ = (((*(source+width)) + ((*source)<<1) + *(source+1))>>2);
      *source++;                                    
   } 
   *target++ = (((*(source+width)) + ((*source)<<1) + *(source-1))>>2);
   *target++ = (((*(source+width)) + ((*source)<<1) + *(source-1))>>2);
   *source++;     
      
   for(register int y=2;y<(height*renderer.gameboyFilterHeight)-2;y+=2) 
   { 
      target = pointer+y*pitch;
      source = init+(y>>1)*width;
      
      *target++ = (((*(source-width)) + ((*source)<<1) + *(source+1))>>2);
      *target++ = (((*(source-width)) + ((*source)<<1) + *(source+1))>>2);
      *source++;                  
          
      for(int x=1;x<width-1;x++)
      {
         *target++ = (((*(source-width)) + ((*source)<<1) + *(source-1))>>2);
         *target++ = (((*(source-width)) + ((*source)<<1) + *(source+1))>>2);
         *source++;                                    
      } 
      *target++ = (((*(source-width)) + ((*source)<<1) + *(source-1))>>2);
      *target++ = (((*(source-width)) + ((*source)<<1) + *(source-1))>>2);
      *source++;                     
      
      target = pointer+(y+1)*pitch;
      source = init+(y>>1)*width;
            
      *target++ = (((*(source+width)) + ((*source)<<1) + *(source+1))>>2);
      *target++ = (((*(source+width)) + ((*source)<<1) + *(source+1))>>2);
      *source++;    
          
      for(int x=1;x<width-1;x++)
      {
         *target++ = (((*(source+width)) + ((*source)<<1) + *(source-1))>>2);
         *target++ = (((*(source+width)) + ((*source)<<1) + *(source+1))>>2);
         *source++;                                    
      } 
      *target++ = (((*(source+width)) + ((*source)<<1) + *(source-1))>>2);
      *target++ = (((*(source+width)) + ((*source)<<1) + *(source-1))>>2);
      *source++;           
   } 

   target = pointer+(height*renderer.gameboyFilterHeight-2)*pitch;
   source = init+(height-1)*width; 

   *target++ = (((*(source-width)) + ((*source)<<1) + *(source+1))>>2);
   *target++ = (((*(source-width)) + ((*source)<<1) + *(source+1))>>2);
   *source++;                 
   for(int x=1;x<width-1;x++)
   {
      *target++ = (((*(source-width)) + ((*source)<<1) + *(source-1))>>2);
      *target++ = (((*(source-width)) + ((*source)<<1) + *(source+1))>>2);
      *source++;                                    
   } 
   *target++ = (((*(source-width)) + ((*source)<<1) + *(source-1))>>2);
   *target++ = (((*(source-width)) + ((*source)<<1) + *(source-1))>>2);
   *source++;       
      
   target = pointer+(height*renderer.gameboyFilterHeight-1)*pitch;
   source = init+(height-1)*width;   

   *target++ = (((*(source-width)) + ((*source)<<1) + *(source+1))>>2);
   *target++ = (((*(source-width)) + ((*source)<<1) + *(source+1))>>2);
   *source++;   
   for(int x=1;x<width-1;x++)
   {
      *target++ = (((*(source-width)) + ((*source)<<1) + *(source-1))>>2);
      *target++ = (((*(source-width)) + ((*source)<<1) + *(source+1))>>2);
      *source++;                                    
   } 
   *target++ = (((*(source-width)) + ((*source)<<1) + *(source-1))>>2);
   *target++ = (((*(source-width)) + ((*source)<<1) + *(source-1))>>2);
   //*source++;         
}

void blur_16(WORD *pointer,WORD *source,int width,int height,int pitch)
{
   register WORD *target=pointer;
   WORD* init = source;
   int mask = ~RGB_BIT_MASK;

   *target++ = (((((*(source+width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source+1))&mask)>>1)&mask)>>1);
   *target++ = (((((*(source+width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source+1))&mask)>>1)&mask)>>1);
   *source++; 
   for(int x=1;x<width-1;x++)
   {
      *target++ = (((((*(source+width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source-1))&mask)>>1)&mask)>>1);
      *target++ = (((((*(source+width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source+1))&mask)>>1)&mask)>>1);
      *source++;                                    
   } 
   *target++ = (((((*(source+width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source-1))&mask)>>1)&mask)>>1);
   *target++ = (((((*(source+width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source-1))&mask)>>1)&mask)>>1);
   *source++;  
      
   target = pointer+pitch;      
   source=init; 
   
   *target++ = (((((*(source+width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source+1))&mask)>>1)&mask)>>1);
   *target++ = (((((*(source+width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source+1))&mask)>>1)&mask)>>1);
   *source++; 
   for(int x=1;x<width-1;x++)
   {
      *target++ = (((((*(source+width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source-1))&mask)>>1)&mask)>>1);
      *target++ = (((((*(source+width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source+1))&mask)>>1)&mask)>>1);      
      *source++;                                    
   } 
   *target++ = (((((*(source+width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source-1))&mask)>>1)&mask)>>1);
   *target++ = (((((*(source+width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source-1))&mask)>>1)&mask)>>1);
   *source++;     
      
   for(register int y=2;y<(height*renderer.gameboyFilterHeight)-2;y+=2) 
   { 
      target = pointer+y*pitch;
      source = init+(y>>1)*width;
      
      *target++ = (((((*(source-width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source+1))&mask)>>1)&mask)>>1);
      *target++ = (((((*(source-width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source+1))&mask)>>1)&mask)>>1);      
      *source++;                  
      for(int x=1;x<width-1;x++)
      {
         *target++ = (((((*(source-width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source-1))&mask)>>1)&mask)>>1);
         *target++ = (((((*(source-width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source+1))&mask)>>1)&mask)>>1);
         *source++;                                    
      } 
      *target++ = (((((*(source-width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source-1))&mask)>>1)&mask)>>1);
      *target++ = (((((*(source-width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source-1))&mask)>>1)&mask)>>1);      
      *source++;                     
      
      target = pointer+(y+1)*pitch;
      source = init+(y>>1)*width;
            
      *target++ = (((((*(source+width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source+1))&mask)>>1)&mask)>>1);
      *target++ = (((((*(source+width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source+1))&mask)>>1)&mask)>>1);      
      *source++;     
      for(int x=1;x<width-1;x++)
      {
         *target++ = (((((*(source+width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source-1))&mask)>>1)&mask)>>1);
         *target++ = (((((*(source+width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source+1))&mask)>>1)&mask)>>1);
         *source++;                                    
      } 
      *target++ = (((((*(source+width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source-1))&mask)>>1)&mask)>>1);
      *target++ = (((((*(source+width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source-1))&mask)>>1)&mask)>>1);      
      *source++;           
   } 

   target = pointer+(height*renderer.gameboyFilterHeight-2)*pitch;
   source = init+(height-1)*width; 

   *target++ = (((((*(source-width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source+1))&mask)>>1)&mask)>>1);
   *target++ = (((((*(source-width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source+1))&mask)>>1)&mask)>>1);
   *source++;               
   for(int x=1;x<width-1;x++)
   {
      *target++ = (((((*(source-width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source-1))&mask)>>1)&mask)>>1);
      *target++ = (((((*(source-width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source+1))&mask)>>1)&mask)>>1);
      *source++;                                    
   } 
   *target++ = (((((*(source-width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source-1))&mask)>>1)&mask)>>1);
   *target++ = (((((*(source-width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source-1))&mask)>>1)&mask)>>1);
   *source++;               
    
      
   target = pointer+(height*renderer.gameboyFilterHeight-1)*pitch;
   source = init+(height-1)*width;   

   *target++ = (((((*(source-width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source+1))&mask)>>1)&mask)>>1);
   *target++ = (((((*(source-width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source+1))&mask)>>1)&mask)>>1);
   *source++;               
   for(int x=1;x<width-1;x++)
   {
      *target++ = (((((*(source-width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source-1))&mask)>>1)&mask)>>1);
      *target++ = (((((*(source-width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source+1))&mask)>>1)&mask)>>1);
      *source++;                                    
   } 
   *target++ = (((((*(source-width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source-1))&mask)>>1)&mask)>>1);
   *target++ = (((((*(source-width))&mask)>>1)&mask)>>1) + ((((*source)&mask)&mask)>>1) + (((((*(source-1))&mask)>>1)&mask)>>1);
   //*source++;                    
}
*/




#ifdef ALLOW_DEBUG
void draw_debug_screen()
{
   DDBLTFX clrblt;
   ZeroMemory(&clrblt,sizeof(DDBLTFX));
   clrblt.dwSize=sizeof(DDBLTFX);
   clrblt.dwFillColor=RGB(0,0,0);
   BSurface->Blt(NULL,NULL,NULL,DDBLT_COLORFILL,&clrblt);

   char chregs[60];
   HDC aDC;

   if(BSurface->GetDC(&aDC)==DD_OK)
   {
      SetBkColor(aDC, RGB(0,0,0));//TRANSPARENT);
      SetTextColor(aDC,RGB(255,255,255));
      sprintf(chregs,"A:  %X | BC: %X", A,BC.W);
      TextOut(aDC,5,0,chregs,strlen(chregs));
      sprintf(chregs,"DE: %X | HL: %X", DE.W,HL.W);
      TextOut(aDC,5,20,chregs,strlen(chregs));
      sprintf(chregs,"PC: %X | F: %X | SP: %X", PC.W,F,SP.W);
      TextOut(aDC,5,40,chregs,strlen(chregs));
      sprintf(chregs,"opcode: %X", opcode);
      TextOut(aDC,5,60,chregs,strlen(chregs));
                     
      sprintf(chregs,"C: %X | H: %X | Z: %X | N: %X", CFLAG,HFLAG,ZFLAG,NFLAG);
      TextOut(aDC,5,80,chregs,strlen(chregs));
           
      sprintf(chregs,"IME: %X",IME);
      TextOut(aDC,5,100,chregs,strlen(chregs));

      BSurface->ReleaseDC(aDC);
   }
        
   if(DDSurface->Blt(&renderer.targetBltRect,BSurface,NULL,0,NULL) == DDERR_SURFACELOST)
   {
      DDSurface->Restore();
      BSurface->Restore();
   }
}
#endif


DirectDraw::DirectDraw(HWND* inHwnd)
{
   //debug_print("Emu Center HX DirectDraw ON");
   borderFilterWidth = borderFilterHeight = gameboyFilterWidth = gameboyFilterHeight = 1;
   borderFilterType = gameboyFilterType = VIDEO_FILTER_NONE;
   hwnd = inHwnd;
   RECT targetBltRect;
   lPitch = 160;
   changeRect = 0;
}

DirectDraw::~DirectDraw()
{
    if(gfx_pal32 != NULL) { 
        delete [] gfx_pal32; 
        gfx_pal32 = NULL; 
    }
    if(gfx_pal16 != NULL) { 
        delete [] gfx_pal16; 
        gfx_pal16 = NULL; 
    }
    if(dxBufferMix != NULL) { 
        if(bitCount==16) {
            delete [] (WORD*)dxBufferMix;
        } else {
            delete [] (DWORD*)dxBufferMix;
        }
        dxBufferMix = NULL; 
    }         
    if(dxBorderBufferRender != NULL) { 
        if(bitCount==16) {
            delete [] (WORD*)dxBorderBufferRender;
        } else {
            delete [] (DWORD*)dxBorderBufferRender;
        }
        dxBorderBufferRender = NULL; 
    }   
      
    SafeRelease(bSurface);
    SafeRelease(borderSurface);
    SafeRelease(ddSurface);
    SafeRelease(ddClip);
    SafeRelease(dd);
    
    DeleteObject(afont);
}

void DirectDraw::setDrawMode(bool mix) 
{
	if (!mix) {
		if(bitCount==16) {
			drawScreen = &DirectDraw::drawScreen16;
		} else {
			drawScreen = &DirectDraw::drawScreen32;
		}
	} else {
		if(bitCount==16) {
			drawScreen = &DirectDraw::drawScreenMix16;
		} else {
			drawScreen = &DirectDraw::drawScreenMix32;
		}
	}
	
}

bool DirectDraw::init()
{
    HRESULT ddrval;
    DDSURFACEDESC2 ddsd;
    //DDSCAPS2 ddscaps;
    
    ddrval = DirectDrawCreateEx(NULL, (void**)&dd, IID_IDirectDraw7, NULL); 
    if(ddrval!=DD_OK)
    {
        debug_print("DirectDraw Create failed!"); 
        return false;
    }
    ddrval = dd->SetCooperativeLevel(*hwnd, DDSCL_NORMAL);
    if(ddrval!=DD_OK)
    {
        debug_print("DirectDraw: SetCooperativelevel failed!"); 
        return false;
    }
    
    ddrval = dd->CreateClipper(0,&ddClip,NULL);
    if(ddrval!=DD_OK)
    {
        debug_print("DirectDraw: CreateClipper failed!"); 
        return false;
    }
    ddClip->SetHWnd(0,*hwnd);
    
    ZeroMemory(&ddsd,sizeof(ddsd));
    ddsd.dwSize = sizeof(DDSURFACEDESC2);
    ddsd.dwFlags = DDSD_CAPS;
    ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    ddrval = dd->CreateSurface(&ddsd,&ddSurface,NULL);
    if(ddrval != DD_OK) 
    {
        debug_print("DirectDraw: Create main surface failed!"); 
        return false;
    }
    
    renderer.ddSurface->SetClipper(renderer.ddClip);
    
    ZeroMemory(&ddsd,sizeof(ddsd));
    ddsd.dwSize = sizeof(DDSURFACEDESC2);
    ddsd.dwFlags = DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT;
    ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN|DDSCAPS_VIDEOMEMORY;//DDSCAPS_SYSTEMMEMORY;
    ddsd.dwWidth = 160;
    ddsd.dwHeight = 144;
    
    ddrval = dd->CreateSurface(&ddsd,&bSurface,NULL);
    if(ddrval != DD_OK) 
    {
        debug_print("DirectDraw: Create gb surface failed!"); 
        return false;
    }
    ddsd.dwWidth = 256;
    ddsd.dwHeight = 224;   
    ddrval = dd->CreateSurface(&ddsd,&borderSurface,NULL);
    if(ddrval != DD_OK) 
    {
        debug_print("DirectDraw: Create border surface failed!"); 
        return false;
    }   
    
    // empty the new surface
    DDBLTFX clrblt;
    ZeroMemory(&clrblt,sizeof(DDBLTFX));
    clrblt.dwSize=sizeof(DDBLTFX);
    clrblt.dwFillColor = RGB(0,0,0);
    bSurface->Blt(NULL,NULL,NULL,DDBLT_COLORFILL,&clrblt);
    borderSurface->Blt(NULL,NULL,NULL,DDBLT_COLORFILL,&clrblt);
    
    ZeroMemory(&ddsd,sizeof(ddsd));
    ddsd.dwSize = sizeof(DDSURFACEDESC2);
    ddsd.dwFlags = DDSD_PIXELFORMAT;
    bSurface->Lock(NULL,&ddsd,DDLOCK_WAIT|DDLOCK_SURFACEMEMORYPTR,NULL);
    
    bitCount = ddsd.ddpfPixelFormat.dwRGBBitCount; 
    lPitch = ddsd.lPitch;
    
    bSurface->Unlock(NULL);
    
    initPaletteShifts();
    
    if (!initPalettes()) return false;
    
	setDrawMode(false);
    
    if(bitCount  == 16) {
        dxBufferMix = new WORD[140*166];     
        dxBorderBufferRender = new WORD[256*224];
        
        drawBorder = &DirectDraw::drawBorder16;
        gameboyFilter16 = &filter_none_16;
        
        lPitch >>= 1;
    } else {
        dxBufferMix = new DWORD[140*166];  
        dxBorderBufferRender = new DWORD[256*224];
        
        drawBorder = &DirectDraw::drawBorder32;
        gameboyFilter32 = &filter_none_32;     
        
        lPitch >>= 2;
    }
    
    if(!dxBufferMix || !dxBorderBufferRender) {
        debug_print(str_table[ERROR_MEMORY]); 
        return false;
    }
    
    SetCurrentDirectory(options->program_directory.c_str());
    AddFontResource(L"PCPaintBoldSmall.ttf");
    
    return true;
}

void DirectDraw::initPaletteShifts()
{
    DDPIXELFORMAT px;
    
    px.dwSize = sizeof(px);
    
    bSurface->GetPixelFormat(&px);
    
    rs = ffs(px.dwRBitMask);
    gs = ffs(px.dwGBitMask);
    bs = ffs(px.dwBBitMask);
    
    RGB_BIT_MASK = 0x421;
    
    if((px.dwFlags&DDPF_RGB) != 0 && px.dwRBitMask == 0xF800 && px.dwGBitMask == 0x07E0 && px.dwBBitMask == 0x001F) {
        gs++;
        RGB_BIT_MASK = 0x821;
    } else if((px.dwFlags&DDPF_RGB) != 0 && px.dwRBitMask == 0x001F && px.dwGBitMask == 0x07E0 && px.dwBBitMask == 0xF800) {
        gs++;
        RGB_BIT_MASK = 0x821;
    } else if(bitCount == 32 || renderer.bitCount == 24) {// 32-bit or 24-bit
        rs += 3;
        gs += 3;
        bs += 3;
    }
}

int DirectDraw::ffs(UINT mask)
{
    int m = 0;
    if(mask) {
        while (!(mask & (1 << m)))
            m++;
        return m;
    }
    return 0;
}

void DirectDraw::showMessage(wstring message, int duration, gb_system* targetGb)
{
    messageText = message;
    messageDuration = duration;
    messageGb = targetGb;
}

void DirectDraw::gbTextOut()
{ // note use of GB here
    if(messageDuration && GB == messageGb) {
        --messageDuration;
        HDC aDC;
        if(bSurface->GetDC(&aDC)==DD_OK) {
            SelectObject(aDC,afont);
            SetBkMode(aDC, TRANSPARENT);
            SetTextColor(aDC,RGB(255,0,128));

            TextOut(aDC,3*gameboyFilterWidth,3*gameboyFilterHeight,messageText.c_str(),messageText.length());
            TextOut(aDC,1*gameboyFilterWidth,1*gameboyFilterWidth,messageText.c_str(),messageText.length());
            TextOut(aDC,1*gameboyFilterWidth,3*gameboyFilterWidth,messageText.c_str(),messageText.length());
            TextOut(aDC,3*gameboyFilterWidth,1*gameboyFilterWidth,messageText.c_str(),messageText.length());
            
            TextOut(aDC,3*gameboyFilterWidth,2*gameboyFilterHeight,messageText.c_str(),messageText.length());
            TextOut(aDC,1*gameboyFilterWidth,2*gameboyFilterWidth,messageText.c_str(),messageText.length());
            TextOut(aDC,2*gameboyFilterWidth,3*gameboyFilterWidth,messageText.c_str(),messageText.length());
            TextOut(aDC,2*gameboyFilterWidth,1*gameboyFilterWidth,messageText.c_str(),messageText.length());
            
            SetTextColor(aDC,RGB(255,255,255));
            TextOut(aDC,2*gameboyFilterWidth,2*gameboyFilterWidth,messageText.c_str(),messageText.length());
            renderer.bSurface->ReleaseDC(aDC);
        }
    }

}

// get the filter width/height for the selected filter type (currently always the same)
int DirectDraw::getFilterDimension(videofiltertype type)
{
    switch (type) {
        case VIDEO_FILTER_SOFTXX:
            return 8;
        case VIDEO_FILTER_SCALE2X:
        case VIDEO_FILTER_SOFT2X:
        case VIDEO_FILTER_BLUR:
            return 2;
        case VIDEO_FILTER_SCALE3X:
            return 3;
        case VIDEO_FILTER_NONE:
        default:
            return 1;
    }
}

void DirectDraw::setBorderFilter(videofiltertype type) 
{
    borderFilterWidth = borderFilterHeight = getFilterDimension(type);
    borderFilterType = type;
    changeFilters();
}

void DirectDraw::setGameboyFilter(videofiltertype type) 
{
    gameboyFilterWidth = gameboyFilterHeight = getFilterDimension(type);
    gameboyFilterType = type;
    changeFilters();
}

bool DirectDraw::changeFilters()
{
	HRESULT ddrval;
	DDSURFACEDESC2 ddsd;
	
	SafeRelease(bSurface);
	SafeRelease(borderSurface);
	
	ZeroMemory(&ddsd,sizeof(ddsd));
	ddsd.dwSize = sizeof(DDSURFACEDESC2);
	ddsd.dwFlags = DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT;
	ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN|DDSCAPS_VIDEOMEMORY;//DDSCAPS_SYSTEMMEMORY;
	ddsd.dwWidth = 160*gameboyFilterWidth;
	ddsd.dwHeight = 144*gameboyFilterHeight;

	ddrval = dd->CreateSurface(&ddsd,&bSurface,NULL);
	if(ddrval != DD_OK) {
		debug_print("DirectDraw Createsurface failed!"); 
		return false;
	}

	ddsd.dwWidth = 256*borderFilterWidth;
	ddsd.dwHeight = 224*borderFilterHeight;
	ddrval = dd->CreateSurface(&ddsd,&borderSurface,NULL);
	if(ddrval != DD_OK)  {
		debug_print("DirectDraw Createsurface failed!"); 
		return false;
	} 
	   
	ZeroMemory(&ddsd,sizeof(ddsd));
	ddsd.dwSize = sizeof(DDSURFACEDESC2);
	bSurface->Lock(NULL,&ddsd,DDLOCK_WAIT|DDLOCK_SURFACEMEMORYPTR,NULL);
   
	lPitch = ddsd.lPitch;
	
	bSurface->Unlock(NULL);
	
	if(bitCount==16){
		lPitch >>= 1;  
       
		switch(gameboyFilterType){
			case VIDEO_FILTER_SOFT2X:
			case VIDEO_FILTER_SOFTXX:
				gameboyFilter16 = softwarexx_16;      
				break;
			case VIDEO_FILTER_SCALE2X:
				gameboyFilter16 = Scale2x16;      
				break;   
			case VIDEO_FILTER_SCALE3X:
				gameboyFilter16 = Scale3x16;      
				break;           
			/*    case VIDEO_FILTER_BLUR:
			gameboyFilter16 = blur_16;    
			break;     */
			case VIDEO_FILTER_NONE:
			default:
				gameboyFilter16 = filter_none_16;
				break;
		}   
		switch(borderFilterType) { 
			case VIDEO_FILTER_SOFT2X:
			case VIDEO_FILTER_SOFTXX:
				borderFilter16 = softwarexx_16;      
				break;
			case VIDEO_FILTER_SCALE2X:
				borderFilter16 = Scale2x16;      
				break;  
			case VIDEO_FILTER_SCALE3X:
				borderFilter16 = Scale3x16;      
				break;            
			/*   case VIDEO_FILTER_BLUR:
			borderFilter16  = blur_16;    
			break;       */
			case VIDEO_FILTER_NONE:
			default:
				borderFilter16 = filter_none_16;
				break;
		}         
	}else{
		lPitch >>= 2;
		
		switch(gameboyFilterType) {
			case VIDEO_FILTER_SOFT2X:
			case VIDEO_FILTER_SOFTXX:
				gameboyFilter32 = softwarexx_32;      
				break;
			case VIDEO_FILTER_SCALE2X:
				gameboyFilter32 = Scale2x32;      
				break;      
			case VIDEO_FILTER_SCALE3X:
				gameboyFilter32 = Scale3x32;      
				break;          
			/*    case VIDEO_FILTER_BLUR:
			gameboyFilter32 = blur_32;    
			break;  */
			case VIDEO_FILTER_NONE:
			default:
				gameboyFilter32 = filter_none_32;
				break;
		}
		switch(borderFilterType) {
			case VIDEO_FILTER_SOFT2X:
			case VIDEO_FILTER_SOFTXX:
				borderFilter32 = softwarexx_32;      
				break;
			case VIDEO_FILTER_SCALE2X:
				borderFilter32 = Scale2x32;      
				break;     
			case VIDEO_FILTER_SCALE3X:
				borderFilter32 = Scale3x32;      
				break;         
			/*   case VIDEO_FILTER_BLUR:
			borderFilter32 = blur_32;    
			break;    */
			case VIDEO_FILTER_NONE:
			default:
				borderFilter32 = filter_none_32;
				break;
		}       
   }    
   if(GB1->romloaded && sgb_mode)
		(this->*DirectDraw::drawBorder)();  // totally not sure about this either 
	
	//afont = CreateFont(12*renderer.gameboyFilterHeight,6*renderer.gameboyFilterWidth,2,2,FW_BOLD,FALSE,FALSE,FALSE,ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH|FF_SWISS,NULL);   
	afont = CreateFont(8*gameboyFilterHeight,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH|FF_SWISS,L"PCPaint Bold Small");   

	return true;
}


int DirectDraw::getBitCount()
{
	return bitCount;
}

void DirectDraw::handleWindowResize()
{
    setRect(false);
    
    // where are we getting multiple_gb and sgb_mode from in this scope
    
    if(multiple_gb) {
        int width = targetBltRect.right - targetBltRect.left;
        targetBltRect.right = targetBltRect.left + width / 2;
    }
    
    if(sgb_mode || (options->GBC_SGB_border != OFF && border_uploaded)) {
        double width = ((double)(targetBltRect.right-targetBltRect.left)/256.0);
        double height = ((double)(targetBltRect.bottom-targetBltRect.top)/224.0);
        
        targetBltRect.left += (long)round(48.0*width); 
        targetBltRect.right = targetBltRect.left + (long)round(160.0*width); 
        targetBltRect.top += (long)round(40.0*height);
        targetBltRect.bottom = targetBltRect.top + (long)round(144.0*height);
        
        (this->*DirectDraw::drawBorder)();  
        if(sgb_mask == 1) (*this.*drawScreen)();
    }    
}

// Does something with a rectangle or idfk
// Also should we use something else instead of options->video_size
void DirectDraw::setRect(bool gb2open)
{
    POINT pt;
    GetClientRect(*hwnd,&targetBltRect);
    pt.x=pt.y=0;
    ClientToScreen(*hwnd,&pt);
    OffsetRect(&targetBltRect,pt.x,pt.y);       
    if (gb2open) {
        targetBltRect.right-=160*options->video_size;
    }
}

// draw the screen without mixing frames
void DirectDraw::drawScreen32()
{  // should the buffer/s maybe be passed into these?
   drawScreenGeneric32((DWORD*)GB->gfx_buffer);
}

// draw the screen mixing frames
void DirectDraw::drawScreenMix32()
{   
	DWORD* current = (DWORD*)GB->gfx_buffer;
	DWORD* old = (DWORD*)GB->gfx_buffer_old;
	DWORD* older = (DWORD*)GB->gfx_buffer_older;
	DWORD* oldest = (DWORD*)GB->gfx_buffer_oldest;
	
	DWORD* target = (DWORD*)dxBufferMix;
	
	DWORD mix_temp1 = 0;
	DWORD mix_temp2 = 0;
	
	if(options->video_mix_frames == MIX_FRAMES_MORE && !(GB->gbc_mode || sgb_mode)) { // Options and modes and stuff ugh
		for(int y = 0;y < 144*160;y++) {// mix it
			mix_temp1 = ((*current) + (*old)) >> 1;
			mix_temp2 = ((*older) + (*oldest)) >> 1;
			
			*target = ((mix_temp1*3 + mix_temp2) >> 2);
			
			++target;
			++current;
			++old;
			++older;
			++oldest;
		}
	
		void* temp1 = GB->gfx_buffer;
		void* temp2 = GB->gfx_buffer_older;
		GB->gfx_buffer = GB->gfx_buffer_oldest;
		GB->gfx_buffer_older = GB->gfx_buffer_old;
		GB->gfx_buffer_old = temp1;
		GB->gfx_buffer_oldest = temp2;
	} else {
		for(int y = 0;y < 144*160;y++) {// mix it
			*target++ = ((*current++) + (*old++)) >> 1;
		}
		
		void* temp = GB->gfx_buffer;
		GB->gfx_buffer = GB->gfx_buffer_old;
		GB->gfx_buffer_old = temp;
	}

    drawScreenGeneric32((DWORD*)dxBufferMix);
}

void DirectDraw::drawScreen16()
{  
   drawScreenGeneric16((WORD*)GB->gfx_buffer);
}

void DirectDraw::drawScreenMix16()
{
	WORD* current = (WORD*)GB->gfx_buffer;
	WORD* old = (WORD*)GB->gfx_buffer_old;
	WORD* older = (WORD*)GB->gfx_buffer_older;
	WORD* oldest = (WORD*)GB->gfx_buffer_oldest;
	
	WORD* target = (WORD*)dxBufferMix;
	
	WORD mix_temp1 = 0;
	WORD mix_temp2 = 0;
	
	WORD mask = ~RGB_BIT_MASK;
   
 /*  for(register int y=0;y<144*160;y+=10) // mix it
   { 
      *target++ = (((*mix_source++)&mask)>>1)+(((*old++)&mask)>>1);
      *target++ = (((*mix_source++)&mask)>>1)+(((*old++)&mask)>>1);
      *target++ = (((*mix_source++)&mask)>>1)+(((*old++)&mask)>>1);
      *target++ = (((*mix_source++)&mask)>>1)+(((*old++)&mask)>>1);
      *target++ = (((*mix_source++)&mask)>>1)+(((*old++)&mask)>>1);
      *target++ = (((*mix_source++)&mask)>>1)+(((*old++)&mask)>>1);
      *target++ = (((*mix_source++)&mask)>>1)+(((*old++)&mask)>>1);
      *target++ = (((*mix_source++)&mask)>>1)+(((*old++)&mask)>>1);
      *target++ = (((*mix_source++)&mask)>>1)+(((*old++)&mask)>>1);
      *target++ = (((*mix_source++)&mask)>>1)+(((*old++)&mask)>>1);
   }*/

	if(options->video_mix_frames == MIX_FRAMES_MORE && !(GB->gbc_mode || sgb_mode)) {
		for(int y = 0;y < 144*160;y++) {// mix it
			
			mix_temp1 = ((*current&mask)>>1) + ((*old&mask)>>1);
			mix_temp2 = ((*older&mask)>>1) + ((*oldest&mask)>>1);
			
			*target++ = ((((mix_temp1&mask)>>1) + ((mix_temp1&mask)>>1)&mask)>>1) +
			((((mix_temp1&mask)>>1) + ((mix_temp2&mask)>>1)&mask)>>1);
			
			++current;
			++old;
			++older;
			++oldest;
		}
		
		void* temp1 = GB->gfx_buffer;
		void* temp2 = GB->gfx_buffer_older;
		GB->gfx_buffer = GB->gfx_buffer_oldest;
		GB->gfx_buffer_older = GB->gfx_buffer_old;
		GB->gfx_buffer_old = temp1;
		GB->gfx_buffer_oldest = temp2;
	} else {
		for(int y = 0;y < 144*160;y++){ // mix it
			*target++ = (((*current++)&mask)>>1)+(((*old++)&mask)>>1);
		}
		
		void* temp = GB->gfx_buffer;
		GB->gfx_buffer = GB->gfx_buffer_old;
		GB->gfx_buffer_old = temp;
	}
   
	renderer.drawScreenGeneric16((WORD*)dxBufferMix);
}


void DirectDraw::drawScreenGeneric32(DWORD* buffer)
{
	DDSURFACEDESC2 ddsd;
	
	ZeroMemory(&ddsd,sizeof(ddsd));
	ddsd.dwSize = sizeof(DDSURFACEDESC2);
	renderer.bSurface->Lock(NULL,&ddsd,DDLOCK_WRITEONLY|DDLOCK_SURFACEMEMORYPTR,NULL);
	
	gameboyFilter32((DWORD*)ddsd.lpSurface,buffer,160,144,lPitch);
	
	renderer.bSurface->Unlock(NULL);   
	// Options accessed in here
	if(options->video_visual_rumble && GB->rumble_counter) {
		--GB->rumble_counter;
		if(!(GB->rumble_counter%2)) {
			targetBltRect.left-=VISUAL_RUMBLE_STRENGTH;
			targetBltRect.right-=VISUAL_RUMBLE_STRENGTH;
			changeRect = 1;
		} else changeRect = 0;
	} else changeRect = 0;
	renderer.gbTextOut();
	
	int screen_real_width = targetBltRect.right - targetBltRect.left;
	// multiple_gb accessed
	if(multiple_gb && GB == GB2) {
		targetBltRect.left += screen_real_width;
		targetBltRect.right += screen_real_width;
	}
	
	if(ddSurface->Blt(&targetBltRect,bSurface,NULL,0,NULL) == DDERR_SURFACELOST)	{
		ddSurface->Restore();
		bSurface->Restore();
	}
	
	if(multiple_gb && GB == GB2) {
		targetBltRect.left -= screen_real_width;
		targetBltRect.right -= screen_real_width;
	} 
	
	if(changeRect){
		targetBltRect.left += VISUAL_RUMBLE_STRENGTH;
		targetBltRect.right += VISUAL_RUMBLE_STRENGTH;
    }
}


void DirectDraw::drawScreenGeneric16(WORD* buffer)
{
	DDSURFACEDESC2 ddsd;
	
	ZeroMemory(&ddsd,sizeof(ddsd));
	ddsd.dwSize = sizeof(DDSURFACEDESC2);
	bSurface->Lock(NULL,&ddsd,DDLOCK_WRITEONLY|DDLOCK_SURFACEMEMORYPTR,NULL);
	
	gameboyFilter16((WORD*)ddsd.lpSurface,buffer,160,144,lPitch);
	
	bSurface->Unlock(NULL);   
	
	if(options->video_visual_rumble && GB->rumble_counter) {
		--GB->rumble_counter;
		if(!(GB->rumble_counter%2)){
			targetBltRect.left-=VISUAL_RUMBLE_STRENGTH;
			targetBltRect.right-=VISUAL_RUMBLE_STRENGTH;
			changeRect = 1;
		} else changeRect = 0;
	} else changeRect = 0;
	
	renderer.gbTextOut();
	
	int screen_real_width = targetBltRect.right - targetBltRect.left;
	
	if(multiple_gb && GB == GB2) {
		targetBltRect.left += screen_real_width;
		targetBltRect.right += screen_real_width;
	}
	
	if(ddSurface->Blt(&targetBltRect,bSurface,NULL,0,NULL) == DDERR_SURFACELOST) {
		ddSurface->Restore();
		bSurface->Restore();
	}
	
	if(multiple_gb && GB == GB2) {
		targetBltRect.left -= screen_real_width;
		targetBltRect.right -= screen_real_width;
	} 
	
	if(changeRect) {
		targetBltRect.left+=VISUAL_RUMBLE_STRENGTH;
		targetBltRect.right+=VISUAL_RUMBLE_STRENGTH;
	}   
}

void DirectDraw::drawBorder32()
{
	unsigned short* source = sgb_border_buffer; // sgb_border_buffer == ?
	DWORD* target = (DWORD*)dxBorderBufferRender;
	
	for(register int y=0;y<256*224;y+=8) { 
		*target++ = *(gfx_pal32+*source++); // gfx_pal32 used here <<<
		*target++ = *(gfx_pal32+*source++);
		*target++ = *(gfx_pal32+*source++);
		*target++ = *(gfx_pal32+*source++);
		*target++ = *(gfx_pal32+*source++);
		*target++ = *(gfx_pal32+*source++);
		*target++ = *(gfx_pal32+*source++);
		*target++ = *(gfx_pal32+*source++);                                                                                                                                                   
	} 
	
	DDSURFACEDESC2 ddsd;
	
	ZeroMemory(&ddsd,sizeof(ddsd));
	ddsd.dwSize = sizeof(DDSURFACEDESC2);
	borderSurface->Lock(NULL,&ddsd,DDLOCK_WRITEONLY|DDLOCK_SURFACEMEMORYPTR,NULL);
	borderLPitch = ddsd.lPitch>>2;
	
	int temp_w = gameboyFilterWidth;
	int temp_h = gameboyFilterHeight;   
	gameboyFilterWidth = borderFilterWidth;
	gameboyFilterHeight = borderFilterHeight;   
	borderFilter32((DWORD*)ddsd.lpSurface,(DWORD*)dxBorderBufferRender,256,224,borderLPitch);
	gameboyFilterWidth = temp_w;
	gameboyFilterHeight = temp_h;
	
	borderSurface->Unlock(NULL);   
	
	POINT pt;
	RECT rect;
	
	GetClientRect(*renderer.hwnd,&rect);
	pt.x=pt.y=0;
	ClientToScreen(*renderer.hwnd,&pt);
	OffsetRect(&rect,pt.x,pt.y);
	
	if(ddSurface->Blt(&rect,borderSurface,NULL,0,NULL) == DDERR_SURFACELOST){
	    ddSurface->Restore();
		borderSurface->Restore();
	}
}

void DirectDraw::drawBorder16()
{
	WORD* target = (WORD*)dxBorderBufferRender;
	unsigned short* source = sgb_border_buffer;
	
	for(register int y=0;y<256*224;y+=8) { 
		*target++ = *(gfx_pal16+*source++);
		*target++ = *(gfx_pal16+*source++);
		*target++ = *(gfx_pal16+*source++);
		*target++ = *(gfx_pal16+*source++);
		*target++ = *(gfx_pal16+*source++);
		*target++ = *(gfx_pal16+*source++);
		*target++ = *(gfx_pal16+*source++);
		*target++ = *(gfx_pal16+*source++);                                                                                                                                                   
	} 
	
	DDSURFACEDESC2 ddsd;
	
	ZeroMemory(&ddsd,sizeof(ddsd));
	ddsd.dwSize = sizeof(DDSURFACEDESC2);
	borderSurface->Lock(NULL,&ddsd,DDLOCK_WRITEONLY|DDLOCK_SURFACEMEMORYPTR,NULL);
	borderLPitch = ddsd.lPitch>>1;
	
	int temp_w = gameboyFilterWidth;
	int temp_h = gameboyFilterHeight;   
	gameboyFilterWidth = borderFilterWidth;
	gameboyFilterHeight = borderFilterHeight;   
	borderFilter16((WORD*)ddsd.lpSurface,(WORD*)dxBorderBufferRender,256,224,borderLPitch);
	gameboyFilterWidth = temp_w;
	gameboyFilterHeight = temp_h;   
	
	borderSurface->Unlock(NULL);   
	
	POINT pt;
	RECT rect;
	
	GetClientRect(*renderer.hwnd,&rect);
	pt.x=pt.y=0;
	ClientToScreen(*renderer.hwnd,&pt);
	OffsetRect(&rect,pt.x,pt.y);
	
	if(ddSurface->Blt(&rect,borderSurface,NULL,0,NULL) == DDERR_SURFACELOST){
		ddSurface->Restore();
		borderSurface->Restore();
	}
}
