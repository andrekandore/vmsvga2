/*
 *  es137x_xtra.h
 *  EnsoniqAudioPCI
 *
 *  Created by Zenith432 on July 21 2009.
 *  Copyright 2009 Zenith432. All rights reserved.
 *
 */

#ifndef __ES137X_XTRA_H__
#define __ES137X_XTRA_H__

#define MEM_MAP_REG 0x14

/* PCI IDs of supported chips */
#define ES1370_PCI_ID 0x50001274
#define ES1371_PCI_ID 0x13711274
#define ES1371_PCI_ID2 0x13713274
#define CT5880_PCI_ID 0x58801274
#define CT4730_PCI_ID 0x89381102

#define ES1371REV_ES1371_A  0x02
#define ES1371REV_ES1371_B  0x09

#define ES1371REV_ES1373_8  0x08
#define ES1371REV_ES1373_A  0x04
#define ES1371REV_ES1373_B  0x06

#define ES1371REV_CT5880_A  0x07

#define CT5880REV_CT5880_C  0x02
#define CT5880REV_CT5880_D  0x03
#define CT5880REV_CT5880_E  0x04

#define CT4730REV_CT4730_A  0x00

#define ES_DEFAULT_BUFSZ 4096

/* 2 DAC for playback, 1 ADC for record */
#define ES_DAC1		0
#define ES_DAC2		1
#define ES_ADC		2
#define ES_NCHANS	3

#define ES_DMA_SEGS_MIN	2
#define ES_DMA_SEGS_MAX	256
#define ES_BLK_MIN	64
#define ES_BLK_ALIGN	(~(ES_BLK_MIN - 1))

#define ES1370_DAC1_MINSPEED	5512
#define ES1370_DAC1_MAXSPEED	44100

#endif /* __ES137X_XTRA_H__ */
