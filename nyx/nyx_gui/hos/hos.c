/*
 * Copyright (c) 2018 naehrwert
 * Copyright (c) 2018 st4rk
 * Copyright (c) 2018 Ced2911
 * Copyright (c) 2018-2020 CTCaer
 * Copyright (c) 2018 balika011
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>

#include "hos.h"
#include "sept.h"
#include "../config.h"
#include <gfx/di.h>
#include <gfx_utils.h>
#include <mem/heap.h>
#include <mem/mc.h>
#include <mem/smmu.h>
#include <sec/se.h>
#include <sec/se_t210.h>
#include <sec/tsec.h>
#include <soc/bpmp.h>
#include <soc/fuse.h>
#include <soc/pmc.h>
#include <soc/t210.h>
#include <storage/mbr_gpt.h>
#include "../storage/nx_emmc.h"
#include <storage/nx_sd.h>
#include <storage/sdmmc.h>
#include <utils/util.h>

extern hekate_config h_cfg;

static u8 *bis_keys = NULL;

//#define DPRINTF(...) gfx_printf(__VA_ARGS__)
#define DPRINTF(...)

#define PKG2_LOAD_ADDR 0xA9800000

 // Secmon mailbox.
#define SECMON_MB_ADDR  0x40002EF8
#define SECMON7_MB_ADDR 0x400000F8
typedef struct _secmon_mailbox_t
{
	//  < 4.0.0 Signals - 0: Not ready, 1: BCT ready, 2: DRAM and pkg2 ready, 3: Continue boot.
	// >= 4.0.0 Signals - 0: Not ready, 1: BCT ready, 2: DRAM ready, 4: pkg2 ready and continue boot.
	u32 in;
	// Non-zero: Secmon ready.
	u32 out;
} secmon_mailbox_t;

static const u8 keyblob_keyseeds[][0x10] = {
	{ 0xDF, 0x20, 0x6F, 0x59, 0x44, 0x54, 0xEF, 0xDC, 0x70, 0x74, 0x48, 0x3B, 0x0D, 0xED, 0x9F, 0xD3 }, // 1.0.0.
	{ 0x0C, 0x25, 0x61, 0x5D, 0x68, 0x4C, 0xEB, 0x42, 0x1C, 0x23, 0x79, 0xEA, 0x82, 0x25, 0x12, 0xAC }, // 3.0.0.
	{ 0x33, 0x76, 0x85, 0xEE, 0x88, 0x4A, 0xAE, 0x0A, 0xC2, 0x8A, 0xFD, 0x7D, 0x63, 0xC0, 0x43, 0x3B }, // 3.0.1.
	{ 0x2D, 0x1F, 0x48, 0x80, 0xED, 0xEC, 0xED, 0x3E, 0x3C, 0xF2, 0x48, 0xB5, 0x65, 0x7D, 0xF7, 0xBE }, // 4.0.0.
	{ 0xBB, 0x5A, 0x01, 0xF9, 0x88, 0xAF, 0xF5, 0xFC, 0x6C, 0xFF, 0x07, 0x9E, 0x13, 0x3C, 0x39, 0x80 }, // 5.0.0.
	{ 0xD8, 0xCC, 0xE1, 0x26, 0x6A, 0x35, 0x3F, 0xCC, 0x20, 0xF3, 0x2D, 0x3B, 0x51, 0x7D, 0xE9, 0xC0 }  // 6.0.0.
};

static const u8 cmac_keyseed[0x10] =
	{ 0x59, 0xC7, 0xFB, 0x6F, 0xBE, 0x9B, 0xBE, 0x87, 0x65, 0x6B, 0x15, 0xC0, 0x53, 0x73, 0x36, 0xA5 };

static const u8 master_keyseed_retail[0x10] =
	{ 0xD8, 0xA2, 0x41, 0x0A, 0xC6, 0xC5, 0x90, 0x01, 0xC6, 0x1D, 0x6A, 0x26, 0x7C, 0x51, 0x3F, 0x3C };

static const u8 console_keyseed[0x10] =
	{ 0x4F, 0x02, 0x5F, 0x0E, 0xB6, 0x6D, 0x11, 0x0E, 0xDC, 0x32, 0x7D, 0x41, 0x86, 0xC2, 0xF4, 0x78 };

const u8 package2_keyseed[0x10] =
	{ 0xFB, 0x8B, 0x6A, 0x9C, 0x79, 0x00, 0xC8, 0x49, 0xEF, 0xD2, 0x4D, 0x85, 0x4D, 0x30, 0xA0, 0xC7 };

static const u8 master_keyseed_4xx_5xx_610[0x10] =
	{ 0x2D, 0xC1, 0xF4, 0x8D, 0xF3, 0x5B, 0x69, 0x33, 0x42, 0x10, 0xAC, 0x65, 0xDA, 0x90, 0x46, 0x66 };

static const u8 master_keyseed_620[0x10] =
	{ 0x37, 0x4B, 0x77, 0x29, 0x59, 0xB4, 0x04, 0x30, 0x81, 0xF6, 0xE5, 0x8C, 0x6D, 0x36, 0x17, 0x9A };

static const u8 console_keyseed_4xx_5xx[0x10] =
	{ 0x0C, 0x91, 0x09, 0xDB, 0x93, 0x93, 0x07, 0x81, 0x07, 0x3C, 0xC4, 0x16, 0x22, 0x7C, 0x6C, 0x28 };

static const u8 mkey_vectors[KB_FIRMWARE_VERSION_MAX + 1][0x10] = {
	{ 0x0C, 0xF0, 0x59, 0xAC, 0x85, 0xF6, 0x26, 0x65, 0xE1, 0xE9, 0x19, 0x55, 0xE6, 0xF2, 0x67, 0x3D }, // Zeroes  encrypted with mkey 00.
	{ 0x29, 0x4C, 0x04, 0xC8, 0xEB, 0x10, 0xED, 0x9D, 0x51, 0x64, 0x97, 0xFB, 0xF3, 0x4D, 0x50, 0xDD }, // Mkey 00 encrypted with mkey 01.
	{ 0xDE, 0xCF, 0xEB, 0xEB, 0x10, 0xAE, 0x74, 0xD8, 0xAD, 0x7C, 0xF4, 0x9E, 0x62, 0xE0, 0xE8, 0x72 }, // Mkey 01 encrypted with mkey 02.
	{ 0x0A, 0x0D, 0xDF, 0x34, 0x22, 0x06, 0x6C, 0xA4, 0xE6, 0xB1, 0xEC, 0x71, 0x85, 0xCA, 0x4E, 0x07 }, // Mkey 02 encrypted with mkey 03.
	{ 0x6E, 0x7D, 0x2D, 0xC3, 0x0F, 0x59, 0xC8, 0xFA, 0x87, 0xA8, 0x2E, 0xD5, 0x89, 0x5E, 0xF3, 0xE9 }, // Mkey 03 encrypted with mkey 04.
	{ 0xEB, 0xF5, 0x6F, 0x83, 0x61, 0x9E, 0xF8, 0xFA, 0xE0, 0x87, 0xD7, 0xA1, 0x4E, 0x25, 0x36, 0xEE }, // Mkey 04 encrypted with mkey 05.
	{ 0x1E, 0x1E, 0x22, 0xC0, 0x5A, 0x33, 0x3C, 0xB9, 0x0B, 0xA9, 0x03, 0x04, 0xBA, 0xDB, 0x07, 0x57 }, // Mkey 05 encrypted with mkey 06.
	{ 0xA4, 0xD4, 0x52, 0x6F, 0xD1, 0xE4, 0x36, 0xAA, 0x9F, 0xCB, 0x61, 0x27, 0x1C, 0x67, 0x65, 0x1F }, // Mkey 06 encrypted with mkey 07.
	{ 0xEA, 0x60, 0xB3, 0xEA, 0xCE, 0x8F, 0x24, 0x46, 0x7D, 0x33, 0x9C, 0xD1, 0xBC, 0x24, 0x98, 0x29 }, // Mkey 07 encrypted with mkey 08.
	{ 0x4D, 0xD9, 0x98, 0x42, 0x45, 0x0D, 0xB1, 0x3C, 0x52, 0x0C, 0x9A, 0x44, 0xBB, 0xAD, 0xAF, 0x80 }, // Mkey 08 encrypted with mkey 09.
	{ 0xB8, 0x96, 0x9E, 0x4A, 0x00, 0x0D, 0xD6, 0x28, 0xB3, 0xD1, 0xDB, 0x68, 0x5F, 0xFB, 0xE1, 0x2A }, // Mkey 09 encrypted with mkey 10.
};

static const u8 new_console_keyseed_4xx[KB_FIRMWARE_VERSION_MAX - KB_FIRMWARE_VERSION_400 + 1][0x10] = {
	{ 0x8B, 0x4E, 0x1C, 0x22, 0x42, 0x07, 0xC8, 0x73, 0x56, 0x94, 0x08, 0x8B, 0xCC, 0x47, 0x0F, 0x5D }, // 4.x   New Device Key Source.
	{ 0x6C, 0xEF, 0xC6, 0x27, 0x8B, 0xEC, 0x8A, 0x91, 0x99, 0xAB, 0x24, 0xAC, 0x4F, 0x1C, 0x8F, 0x1C }, // 5.x   New Device Key Source.
	{ 0x70, 0x08, 0x1B, 0x97, 0x44, 0x64, 0xF8, 0x91, 0x54, 0x9D, 0xC6, 0x84, 0x8F, 0x1A, 0xB2, 0xE4 }, // 6.x   New Device Key Source.
	{ 0x8E, 0x09, 0x1F, 0x7A, 0xBB, 0xCA, 0x6A, 0xFB, 0xB8, 0x9B, 0xD5, 0xC1, 0x25, 0x9C, 0xA9, 0x17 }, // 6.2.0 New Device Key Source.
	{ 0x8F, 0x77, 0x5A, 0x96, 0xB0, 0x94, 0xFD, 0x8D, 0x28, 0xE4, 0x19, 0xC8, 0x16, 0x1C, 0xDB, 0x3D }, // 7.0.0 New Device Key Source.
	{ 0x67, 0x62, 0xD4, 0x8E, 0x55, 0xCF, 0xFF, 0x41, 0x31, 0x15, 0x3B, 0x24, 0x0C, 0x7C, 0x07, 0xAE }, // 8.1.0 New Device Key Source.
	{ 0x4A, 0xC3, 0x4E, 0x14, 0x8B, 0x96, 0x4A, 0xD5, 0xD4, 0x99, 0x73, 0xC4, 0x45, 0xAB, 0x8B, 0x49 }, // 9.0.0 New Device Key Source.
	{ 0x14, 0xB8, 0x74, 0x12, 0xCB, 0xBD, 0x0B, 0x8F, 0x20, 0xFB, 0x30, 0xDA, 0x27, 0xE4, 0x58, 0x94 }, // 9.1.0 New Device Key Source.
};

static const u8 new_console_keyseed[KB_FIRMWARE_VERSION_MAX - KB_FIRMWARE_VERSION_400 + 1][0x10] = {
	{ 0x88, 0x62, 0x34, 0x6E, 0xFA, 0xF7, 0xD8, 0x3F, 0xE1, 0x30, 0x39, 0x50, 0xF0, 0xB7, 0x5D, 0x5D }, // 4.x   New Device Keygen Source.
	{ 0x06, 0x1E, 0x7B, 0xE9, 0x6D, 0x47, 0x8C, 0x77, 0xC5, 0xC8, 0xE7, 0x94, 0x9A, 0xA8, 0x5F, 0x2E }, // 5.x   New Device Keygen Source.
	{ 0x99, 0xFA, 0x98, 0xBD, 0x15, 0x1C, 0x72, 0xFD, 0x7D, 0x9A, 0xD5, 0x41, 0x00, 0xFD, 0xB2, 0xEF }, // 6.x   New Device Keygen Source.
	{ 0x81, 0x3C, 0x6C, 0xBF, 0x5D, 0x21, 0xDE, 0x77, 0x20, 0xD9, 0x6C, 0xE3, 0x22, 0x06, 0xAE, 0xBB }, // 6.2.0 New Device Keygen Source.
	{ 0x86, 0x61, 0xB0, 0x16, 0xFA, 0x7A, 0x9A, 0xEA, 0xF6, 0xF5, 0xBE, 0x1A, 0x13, 0x5B, 0x6D, 0x9E }, // 7.0.0 New Device Keygen Source.
	{ 0xA6, 0x81, 0x71, 0xE7, 0xB5, 0x23, 0x74, 0xB0, 0x39, 0x8C, 0xB7, 0xFF, 0xA0, 0x62, 0x9F, 0x8D }, // 8.1.0 New Device Keygen Source.
	{ 0x03, 0xE7, 0xEB, 0x43, 0x1B, 0xCF, 0x5F, 0xB5, 0xED, 0xDC, 0x97, 0xAE, 0x21, 0x8D, 0x19, 0xED }, // 9.0.0 New Device Keygen Source.
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // TODO: 9.1.0 New Device Keygen Source to be added on next change-of-keys. */
};

static const u8 gen_keyseed[0x10] =
	{ 0x89, 0x61, 0x5E, 0xE0, 0x5C, 0x31, 0xB6, 0x80, 0x5F, 0xE5, 0x8F, 0x3D, 0xA2, 0x4F, 0x7A, 0xA8 };

static const u8 gen_kekseed[0x10] =
	{ 0x4D, 0x87, 0x09, 0x86, 0xC4, 0x5D, 0x20, 0x72, 0x2F, 0xBA, 0x10, 0x53, 0xDA, 0x92, 0xE8, 0xA9 };

static const u8 gen_keyseed_retail[0x10] =
	{ 0xE2, 0xD6, 0xB8, 0x7A, 0x11, 0x9C, 0xB8, 0x80, 0xE8, 0x22, 0x88, 0x8A, 0x46, 0xFB, 0xA1, 0x95 };

static const u8 bis_kekseed[0x10] =
	{ 0x34, 0xC1, 0xA0, 0xC4, 0x82, 0x58, 0xF8, 0xB4, 0xFA, 0x9E, 0x5E, 0x6A, 0xDA, 0xFC, 0x7E, 0x4F };

static const u8 bis_keyseed[][0x10] = {
	{ 0xF8, 0x3F, 0x38, 0x6E, 0x2C, 0xD2, 0xCA, 0x32, 0xA8, 0x9A, 0xB9, 0xAA, 0x29, 0xBF, 0xC7, 0x48 }, // BIS 0 Crypt seed.
	{ 0x7D, 0x92, 0xB0, 0x3A, 0xA8, 0xBF, 0xDE, 0xE1, 0xA7, 0x4C, 0x3B, 0x6E, 0x35, 0xCB, 0x71, 0x06 }, // BIS 0 Tweak seed.
	{ 0x41, 0x00, 0x30, 0x49, 0xDD, 0xCC, 0xC0, 0x65, 0x64, 0x7A, 0x7E, 0xB4, 0x1E, 0xED, 0x9C, 0x5F }, // BIS 1 Crypt seed.
	{ 0x44, 0x42, 0x4E, 0xDA, 0xB4, 0x9D, 0xFC, 0xD9, 0x87, 0x77, 0x24, 0x9A, 0xDC, 0x9F, 0x7C, 0xA4 }, // BIS 1 Tweak seed.
	{ 0x52, 0xC2, 0xE9, 0xEB, 0x09, 0xE3, 0xEE, 0x29, 0x32, 0xA1, 0x0C, 0x1F, 0xB6, 0xA0, 0x92, 0x6C }, // BIS 2/3 Crypt seed.
	{ 0x4D, 0x12, 0xE1, 0x4B, 0x2A, 0x47, 0x4C, 0x1C, 0x09, 0xCB, 0x03, 0x59, 0xF0, 0x15, 0xF4, 0xE4 }  // BIS 2/3 Tweak seed.
};

bool hos_eks_rw_try(u8 *buf, bool write)
{
	for (u32 i = 0; i < 3; i++)
	{
		if (!write)
		{
			if (sdmmc_storage_read(&sd_storage, 0, 1, buf))
				return true;
		}
		else
		{
			if (sdmmc_storage_write(&sd_storage, 0, 1, buf))
				return true;
		}
	}

	return false;
}

void hos_eks_get()
{
	// Check if EKS already found and parsed.
	if (!h_cfg.eks)
	{
		// Read EKS blob.
		u8 *mbr = calloc(512 , 1);
		if (!hos_eks_rw_try(mbr, false))
			goto out;

		// Decrypt EKS blob.
		hos_eks_mbr_t *eks = (hos_eks_mbr_t *)(mbr + 0x60);
		se_aes_crypt_ecb(14, 0, eks, sizeof(hos_eks_mbr_t), eks, sizeof(hos_eks_mbr_t));

		// Check if valid and for this unit.
		if (eks->magic == HOS_EKS_MAGIC &&
			eks->sbk_low == FUSE(FUSE_PRIVATE_KEY0))
		{
			h_cfg.eks = eks;
			return;
		}

out:
		free(mbr);
	}
}

void hos_eks_save(u32 kb)
{
	if (kb >= KB_FIRMWARE_VERSION_700)
	{
		u32 key_idx = 0;
		if (kb >= KB_FIRMWARE_VERSION_810)
			key_idx = 1;

		bool new_eks = false;
		if (!h_cfg.eks)
		{
			h_cfg.eks = calloc(512 , 1);
			new_eks = true;
		}

		// If matching blob doesn't exist, create it.
		bool update_eks = key_idx ? (h_cfg.eks->enabled[key_idx] < kb) : !h_cfg.eks->enabled[0];
		if (update_eks)
		{
			// Read EKS blob.
			u8 *mbr = calloc(512 , 1);
			if (!hos_eks_rw_try(mbr, false))
			{
				if (new_eks)
				{
					free(h_cfg.eks);
					h_cfg.eks = NULL;
				}

				goto out;
			}

			// Get keys.
			u8 *keys = (u8 *)calloc(0x1000, 1);
			se_get_aes_keys(keys + 0x800, keys, 0x10);

			// Set magic and personalized info.
			h_cfg.eks->magic = HOS_EKS_MAGIC;
			h_cfg.eks->enabled[key_idx] = kb;
			h_cfg.eks->sbk_low = FUSE(FUSE_PRIVATE_KEY0);

			// Copy new keys.
			memcpy(h_cfg.eks->dkg, keys + 10 * 0x10, 0x10);
			memcpy(h_cfg.eks->dkk, keys + 15 * 0x10, 0x10);

			if (!h_cfg.aes_slots_new)
			{
				memcpy(h_cfg.eks->keys[key_idx].mkk, keys + 12 * 0x10, 0x10);
				memcpy(h_cfg.eks->keys[key_idx].fdk, keys + 13 * 0x10, 0x10);
			}
			else // New sept slots.
			{
				memcpy(h_cfg.eks->keys[key_idx].mkk, keys + 13 * 0x10, 0x10);
				memcpy(h_cfg.eks->keys[key_idx].fdk, keys + 12 * 0x10, 0x10);
			}

			// Encrypt EKS blob.
			u8 *eks = calloc(512 , 1);
			memcpy(eks, h_cfg.eks, sizeof(hos_eks_mbr_t));
			se_aes_crypt_ecb(14, 1, eks, sizeof(hos_eks_mbr_t), eks, sizeof(hos_eks_mbr_t));

			// Write EKS blob to SD.
			memcpy(mbr + 0x60, eks, sizeof(hos_eks_mbr_t));
			hos_eks_rw_try(mbr, true);


			free(eks);
			free(keys);
out:
			free(mbr);
		}
	}
}

void hos_eks_clear(u32 kb)
{
	if (h_cfg.eks && kb >= KB_FIRMWARE_VERSION_700)
	{
		u32 key_idx = 0;
		if (kb >= KB_FIRMWARE_VERSION_810)
			key_idx = 1;

		// Check if Current Master key is enabled.
		if (h_cfg.eks->enabled[key_idx])
		{
			// Read EKS blob.
			u8 *mbr = calloc(512 , 1);
			if (!hos_eks_rw_try(mbr, false))
				goto out;

			// Disable current Master key version.
			h_cfg.eks->enabled[key_idx] = 0;

			// Encrypt EKS blob.
			u8 *eks = calloc(512 , 1);
			memcpy(eks, h_cfg.eks, sizeof(hos_eks_mbr_t));
			se_aes_crypt_ecb(14, 1, eks, sizeof(hos_eks_mbr_t), eks, sizeof(hos_eks_mbr_t));

			// Write EKS blob to SD.
			memcpy(mbr + 0x60, eks, sizeof(hos_eks_mbr_t));
			hos_eks_rw_try(mbr, true);

			EMC(EMC_SCRATCH0) &= ~EMC_SEPT_RUN;
			h_cfg.sept_run = false;

			free(eks);
out:
			free(mbr);
		}
	}
}

void hos_eks_bis_save()
{
	bool new_eks = false;
	if (!h_cfg.eks)
	{
		h_cfg.eks = calloc(512 , 1);
		new_eks = true;
	}

	// If matching blob doesn't exist, create it.
	if (!h_cfg.eks->enabled_bis)
	{
		// Read EKS blob.
		u8 *mbr = calloc(512 , 1);
		if (!hos_eks_rw_try(mbr, false))
		{
			if (new_eks)
			{
				free(h_cfg.eks);
				h_cfg.eks = NULL;
			}

			goto out;
		}

		// Set magic and personalized info.
		h_cfg.eks->magic = HOS_EKS_MAGIC;
		h_cfg.eks->enabled_bis = 1;
		h_cfg.eks->sbk_low = FUSE(FUSE_PRIVATE_KEY0);

		// Copy new keys.
		memcpy(h_cfg.eks->bis_keys[0].crypt, bis_keys + (0 * 0x10), 0x10);
		memcpy(h_cfg.eks->bis_keys[0].tweak, bis_keys + (1 * 0x10), 0x10);

		memcpy(h_cfg.eks->bis_keys[1].crypt, bis_keys + (2 * 0x10), 0x10);
		memcpy(h_cfg.eks->bis_keys[1].tweak, bis_keys + (3 * 0x10), 0x10);

		memcpy(h_cfg.eks->bis_keys[2].crypt, bis_keys + (4 * 0x10), 0x10);
		memcpy(h_cfg.eks->bis_keys[2].tweak, bis_keys + (5 * 0x10), 0x10);

		// Encrypt EKS blob.
		u8 *eks = calloc(512 , 1);
		memcpy(eks, h_cfg.eks, sizeof(hos_eks_mbr_t));
		se_aes_crypt_ecb(14, 1, eks, sizeof(hos_eks_mbr_t), eks, sizeof(hos_eks_mbr_t));

		// Write EKS blob to SD.
		memcpy(mbr + 0x60, eks, sizeof(hos_eks_mbr_t));
		hos_eks_rw_try(mbr, true);


		free(eks);
out:
		free(mbr);
	}
}

void hos_eks_bis_clear()
{
	// Check if BIS keys are enabled.
	if (h_cfg.eks && h_cfg.eks->enabled_bis)
	{
		// Read EKS blob.
		u8 *mbr = calloc(512 , 1);
		if (!hos_eks_rw_try(mbr, false))
			goto out;

		// Disable BIS storage.
		h_cfg.eks->enabled_bis = 0;

		// Encrypt EKS blob.
		u8 *eks = calloc(512 , 1);
		memcpy(eks, h_cfg.eks, sizeof(hos_eks_mbr_t));
		se_aes_crypt_ecb(14, 1, eks, sizeof(hos_eks_mbr_t), eks, sizeof(hos_eks_mbr_t));

		// Write EKS blob to SD.
		memcpy(mbr + 0x60, eks, sizeof(hos_eks_mbr_t));
		hos_eks_rw_try(mbr, true);

		free(eks);
out:
		free(mbr);
	}
}

int hos_keygen(u8 *keyblob, u32 kb, tsec_ctxt_t *tsec_ctxt)
{
	u8 tmp[0x30];
	u32 retries = 0;

	if (kb > KB_FIRMWARE_VERSION_MAX)
		return 0;

	if (kb <= KB_FIRMWARE_VERSION_600)
		tsec_ctxt->size = 0xF00;
	else if (kb == KB_FIRMWARE_VERSION_620)
		tsec_ctxt->size = 0x2900;
	else if (kb == KB_FIRMWARE_VERSION_700)
		tsec_ctxt->size = 0x3000;
	else
		tsec_ctxt->size = 0x3300;

	// Prepare smmu tsec page for 6.2.0.
	if (kb == KB_FIRMWARE_VERSION_620)
	{
		u8 *tsec_paged = (u8 *)page_alloc(3);
		memcpy(tsec_paged, (void *)tsec_ctxt->fw, tsec_ctxt->size);
		tsec_ctxt->fw = tsec_paged;
	}

	// Get TSEC key.
	if (kb <= KB_FIRMWARE_VERSION_620)
	{
		while (tsec_query(tmp, kb, tsec_ctxt) < 0)
		{
			memset(tmp, 0x00, 0x20);
			retries++;

			// We rely on racing conditions, make sure we cover even the unluckiest cases.
			if (retries > 15)
			{
				EPRINTF("\nFailed to get TSEC keys. Please try again.\n");
				return 0;
			}
		}
	}

	if (kb >= KB_FIRMWARE_VERSION_700)
	{
		// Use HOS EKS if it exists.
		u32 key_idx = 0;
		if (kb >= KB_FIRMWARE_VERSION_810)
			key_idx = 1;

		if (h_cfg.eks && h_cfg.eks->enabled[key_idx] >= kb)
		{
			// Set Device keygen key to slot 10.
			se_aes_key_set(10, h_cfg.eks->dkg, 0x10);
			// Set Master key to slot 12.
			se_aes_key_set(12, h_cfg.eks->keys[key_idx].mkk, 0x10);
			// Set FW Device key key to slot 13.
			se_aes_key_set(13, h_cfg.eks->keys[key_idx].fdk, 0x10);
			// Set Device key to slot 15.
			se_aes_key_set(15, h_cfg.eks->dkk, 0x10);
		}
		else
			h_cfg.aes_slots_new = se_key_acc_ctrl_get(12) == 0x6A;

		se_aes_key_clear(8);
		se_aes_unwrap_key(8, !h_cfg.aes_slots_new ? 12 : 13, package2_keyseed);
	}
	else if (kb == KB_FIRMWARE_VERSION_620)
	{
		// Set TSEC key.
		se_aes_key_set(12, tmp, 0x10);
		// Set TSEC root key.
		se_aes_key_set(13, tmp + 0x10, 0x10);

		// Decrypt keyblob and set keyslots
		se_aes_crypt_block_ecb(12, 0, tmp + 0x20, keyblob_keyseeds[0]);
		se_aes_unwrap_key(15, 14, tmp + 0x20);
		se_aes_unwrap_key(10, 15, console_keyseed_4xx_5xx);
		se_aes_unwrap_key(15, 15, console_keyseed);

		// Package2 key.
		se_aes_unwrap_key(8, 13, master_keyseed_620);
		se_aes_unwrap_key(9, 8, master_keyseed_retail);
		se_aes_unwrap_key(8, 9, package2_keyseed);
	}
	else
	{
		// Set TSEC key.
		se_aes_key_set(13, tmp, 0x10);

		// Derive keyblob keys from TSEC+SBK.
		se_aes_crypt_block_ecb(13, 0, tmp, keyblob_keyseeds[0]);
		se_aes_unwrap_key(15, 14, tmp);
		se_aes_crypt_block_ecb(13, 0, tmp, keyblob_keyseeds[kb]);
		se_aes_unwrap_key(13, 14, tmp);

		// Clear SBK.
		se_aes_key_clear(14);

		//TODO: verify keyblob CMAC.
		//se_aes_unwrap_key(11, 13, cmac_keyseed);
		//se_aes_cmac(tmp, 0x10, 11, keyblob + 0x10, 0xA0);
		//if (!memcmp(keyblob, tmp, 0x10))
		//	return 0;

		se_aes_crypt_block_ecb(13, 0, tmp, cmac_keyseed);
		se_aes_unwrap_key(11, 13, cmac_keyseed);

		// Decrypt keyblob and set keyslots.
		se_aes_crypt_ctr(13, keyblob + 0x20, 0x90, keyblob + 0x20, 0x90, keyblob + 0x10);
		se_aes_key_set(11, keyblob + 0x20 + 0x80, 0x10); // Package1 key.
		se_aes_key_set(12, keyblob + 0x20, 0x10);
		se_aes_key_set(13, keyblob + 0x20, 0x10);

		se_aes_crypt_block_ecb(12, 0, tmp, master_keyseed_retail);

		switch (kb)
		{
		case KB_FIRMWARE_VERSION_100_200:
		case KB_FIRMWARE_VERSION_300:
		case KB_FIRMWARE_VERSION_301:
			se_aes_unwrap_key(13, 15, console_keyseed);
			se_aes_unwrap_key(12, 12, master_keyseed_retail);
			break;
		case KB_FIRMWARE_VERSION_400:
			se_aes_unwrap_key(13, 15, console_keyseed_4xx_5xx);
			se_aes_unwrap_key(15, 15, console_keyseed);
			se_aes_unwrap_key(14, 12, master_keyseed_4xx_5xx_610);
			se_aes_unwrap_key(12, 12, master_keyseed_retail);
			break;
		case KB_FIRMWARE_VERSION_500:
		case KB_FIRMWARE_VERSION_600:
			se_aes_unwrap_key(10, 15, console_keyseed_4xx_5xx);
			se_aes_unwrap_key(15, 15, console_keyseed);
			se_aes_unwrap_key(14, 12, master_keyseed_4xx_5xx_610);
			se_aes_unwrap_key(12, 12, master_keyseed_retail);
			break;
		}

		// Package2 key.
		se_aes_unwrap_key(8, 12, package2_keyseed);
	}

	return 1;
}

static void _hos_validate_sept_mkey(u32 kb)
{
	u8 tmp_mkey[0x10];
	u32 mkey_idx = sizeof(mkey_vectors) / 0x10;
	u8 mkey_slot = !h_cfg.aes_slots_new ? 12 : 13;
	do
	{
		mkey_idx--;
		se_aes_crypt_ecb(mkey_slot, 0, tmp_mkey, 0x10, mkey_vectors[mkey_idx], 0x10);
		for (u32 idx = 0; idx < mkey_idx; idx++)
		{
			se_aes_key_clear(2);
			se_aes_key_set(2, tmp_mkey, 0x10);
			se_aes_crypt_ecb(2, 0, tmp_mkey, 0x10, mkey_vectors[mkey_idx - 1 - idx], 0x10);
		}

		if (!memcmp(tmp_mkey, "\x00\x00\x00\x00\x00\x00\x00\x00", 8))
		{
			se_aes_key_clear(2);
			hos_eks_save(kb);
			return;
		}
	} while (mkey_idx - 1);

	se_aes_key_clear(2);
	hos_eks_clear(kb);
}

int hos_bis_keygen(u8 *keyblob, u32 kb, tsec_ctxt_t *tsec_ctxt)
{
	u32 keygen_rev = 0;
	u32 console_key_slot = kb >= KB_FIRMWARE_VERSION_400 ? 15 : 13;

	if (!bis_keys)
		bis_keys = malloc(0x10 * 6);

	if (!h_cfg.eks || !h_cfg.eks->enabled_bis)
	{
		hos_keygen(keyblob, kb, tsec_ctxt);

		if (kb >= KB_FIRMWARE_VERSION_400)
			keygen_rev = fuse_read_odm_keygen_rev();

		if (keygen_rev)
		{
			// Keygen revision uses bootloader version, which starts from 1.
			keygen_rev -= (KB_FIRMWARE_VERSION_400 + 1);

			// Derive mkey 0.
			u8 tmp_mkey[0x10];
			u32 mkey_idx = sizeof(mkey_vectors) / 0x10;
			u8 mkey_slot = kb >= KB_FIRMWARE_VERSION_700 ? (!h_cfg.aes_slots_new ? 12 : 13) : (kb == KB_FIRMWARE_VERSION_620 ? 9 : 12);
			do
			{
				mkey_idx--;
				se_aes_crypt_ecb(mkey_slot, 0, tmp_mkey, 0x10, mkey_vectors[mkey_idx], 0x10);
				for (u32 idx = 0; idx < mkey_idx; idx++)
				{
					se_aes_key_clear(2);
					se_aes_key_set(2, tmp_mkey, 0x10);
					se_aes_crypt_ecb(2, 0, tmp_mkey, 0x10, mkey_vectors[mkey_idx - 1 - idx], 0x10);
				}
			} while (memcmp(tmp_mkey, "\x00\x00\x00\x00\x00\x00\x00\x00", 8) != 0 && (mkey_idx - 1));

			// Derive new device key.
			se_aes_key_clear(1);
			se_aes_unwrap_key(1, 10, new_console_keyseed_4xx[keygen_rev]); // Uses Device key 4x.
			se_aes_crypt_ecb(10, 0, tmp_mkey, 0x10, new_console_keyseed_4xx[keygen_rev], 0x10); // Uses Device key 4x.
			se_aes_unwrap_key(1, 2, new_console_keyseed[keygen_rev]); // Uses Master Key 0.
			se_aes_unwrap_key(1, 1, tmp_mkey);

			console_key_slot = 1;
		}

		// Generate generic kek.
		se_aes_key_clear(2);
		se_aes_unwrap_key(2, console_key_slot, gen_keyseed_retail);

		// Clear bis keys storage.
		memset(bis_keys, 0, 0x10 * 6);

		// Generate BIS 0 Keys.
		se_aes_crypt_block_ecb(2, 0, bis_keys + (0 * 0x10), bis_keyseed[0]);
		se_aes_crypt_block_ecb(2, 0, bis_keys + (1 * 0x10), bis_keyseed[1]);

		// Generate generic kek.
		se_aes_key_clear(2);
		se_aes_unwrap_key(2, console_key_slot, gen_kekseed);
		se_aes_unwrap_key(2, 2, bis_kekseed);
		se_aes_unwrap_key(2, 2, gen_keyseed);

		// Generate BIS 1 Keys.
		se_aes_crypt_block_ecb(2, 0, bis_keys + (2 * 0x10), bis_keyseed[2]);
		se_aes_crypt_block_ecb(2, 0, bis_keys + (3 * 0x10), bis_keyseed[3]);

		// Generate BIS 2/3 Keys.
		se_aes_crypt_block_ecb(2, 0, bis_keys + (4 * 0x10), bis_keyseed[4]);
		se_aes_crypt_block_ecb(2, 0, bis_keys + (5 * 0x10), bis_keyseed[5]);

		if (kb >= KB_FIRMWARE_VERSION_700)
			_hos_validate_sept_mkey(kb);
	}
	else
	{
		memcpy(bis_keys + (0 * 0x10), h_cfg.eks->bis_keys[0].crypt, 0x10);
		memcpy(bis_keys + (1 * 0x10), h_cfg.eks->bis_keys[0].tweak, 0x10);

		memcpy(bis_keys + (2 * 0x10), h_cfg.eks->bis_keys[1].crypt, 0x10);
		memcpy(bis_keys + (3 * 0x10), h_cfg.eks->bis_keys[1].tweak, 0x10);

		memcpy(bis_keys + (4 * 0x10), h_cfg.eks->bis_keys[2].crypt, 0x10);
		memcpy(bis_keys + (5 * 0x10), h_cfg.eks->bis_keys[2].tweak, 0x10);
	}

	// Clear all AES keyslots.
	for (u32 i = 0; i < 6; i++)
		se_aes_key_clear(i);

	// Set BIS keys.
	se_aes_key_set(0, bis_keys + (0 * 0x10), 0x10);
	se_aes_key_set(1, bis_keys + (1 * 0x10), 0x10);

	se_aes_key_set(2, bis_keys + (2 * 0x10), 0x10);
	se_aes_key_set(3, bis_keys + (3 * 0x10), 0x10);

	se_aes_key_set(4, bis_keys + (4 * 0x10), 0x10);
	se_aes_key_set(5, bis_keys + (5 * 0x10), 0x10);

	return 1;
}

void hos_bis_keys_clear()
{
	// Clear all aes keyslots.
	for (u32 i = 0; i < 6; i++)
		se_aes_key_clear(i);

	// Set SBK back.
	u32 sbk[4] = {
			FUSE(FUSE_PRIVATE_KEY0),
			FUSE(FUSE_PRIVATE_KEY1),
			FUSE(FUSE_PRIVATE_KEY2),
			FUSE(FUSE_PRIVATE_KEY3)
		};
	// Set SBK to slot 14.
	se_aes_key_set(14, sbk, 0x10);

	// Lock SBK from being read.
	se_key_acc_ctrl(14, SE_KEY_TBL_DIS_KEYREAD_FLAG);
}