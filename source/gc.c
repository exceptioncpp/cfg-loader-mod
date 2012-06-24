#include <gccore.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <malloc.h>

#include "gc.h"
#include "dir.h"
#include "util.h"
#include "gettext.h"
#include "wpad.h"
#include "debug.h"
#include "disc.h"
#include "fileOps.h"
#include "cfg.h"

#define MAX_FAT_PATH 1024

#define SRAM_ENGLISH 0
#define SRAM_GERMAN 1
#define SRAM_FRENCH 2
#define SRAM_SPANISH 3
#define SRAM_ITALIAN 4
#define SRAM_DUTCH 5

syssram* __SYS_LockSram();
u32 __SYS_UnlockSram(u32 write);
u32 __SYS_SyncSram(void);

extern char wbfs_fs_drive[16];

void GC_SetVideoMode(u8 videomode)
{
	syssram *sram;
	sram = __SYS_LockSram();
	//void *m_frameBuf;
	static GXRModeObj *rmode;
	int memflag = 0;

	if((VIDEO_HaveComponentCable() && (CONF_GetProgressiveScan() > 0)) || videomode > 3)
		sram->flags |= 0x80; //set progressive flag
	else
		sram->flags &= 0x7F; //clear progressive flag

	if(videomode == 1 || videomode == 3 || videomode == 5)
	{
		memflag = 1;
		sram->flags |= 0x01; // Set bit 0 to set the video mode to PAL
		sram->ntd |= 0x40; //set pal60 flag
	}
	else
	{
		sram->flags &= 0xFE; // Clear bit 0 to set the video mode to NTSC
		sram->ntd &= 0xBF; //clear pal60 flag
	}

	if(videomode == 1)
		rmode = &TVPal528IntDf;
	else if(videomode == 2)
		rmode = &TVNtsc480IntDf;
	else if(videomode == 3)
	{
		rmode = &TVEurgb60Hz480IntDf;
		memflag = 5;
	}
	else if(videomode == 4)
		rmode = &TVNtsc480Prog;
	else if(videomode == 5)
	{
		rmode = &TVEurgb60Hz480Prog;
		memflag = 5;
	}

	__SYS_UnlockSram(1); // 1 -> write changes
	while(!__SYS_SyncSram());

	/* Set video mode to PAL or NTSC */
	*(vu32*)0x800000CC = memflag;
	DCFlushRange((void *)(0x800000CC), 4);
	//ICInvalidateRange((void *)(0x800000CC), 4);

	if (rmode != 0)
		VIDEO_Configure(rmode);
	
	//m_frameBuf = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));

	//VIDEO_ClearFrameBuffer(rmode, m_frameBuf, COLOR_BLACK);
	//VIDEO_SetNextFramebuffer(m_frameBuf);

	VIDEO_SetBlack(TRUE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) 
		VIDEO_WaitVSync();
}

u8 get_wii_language()
{
	switch (CONF_GetLanguage())
	{
		case CONF_LANG_GERMAN:
			return SRAM_GERMAN;
		case CONF_LANG_FRENCH:
			return SRAM_FRENCH;
		case CONF_LANG_SPANISH:
			return SRAM_SPANISH;
		case CONF_LANG_ITALIAN:
			return SRAM_ITALIAN;
		case CONF_LANG_DUTCH:
			return SRAM_DUTCH;
		default:
			return SRAM_ENGLISH;
	}
}

void GC_SetLanguage(u8 lang)
{
	if (lang == 0)
		lang = get_wii_language();
	else
		lang--;

	syssram *sram;
	sram = __SYS_LockSram();
	sram->lang = lang;

	__SYS_UnlockSram(1); // 1 -> write changes
	while(!__SYS_SyncSram());
}

s32 DML_RemoveGame(struct discHdr header, bool onlySD)
{
	char fname[MAX_FAT_PATH];
	
	if (header.magic == DML_MAGIC)
		snprintf(fname, sizeof(fname), "sd:/games/%s/", header.folder);
	else if (header.magic == DML_MAGIC_HDD && !onlySD)
		snprintf(fname, sizeof(fname), "%s/games/%s/", wbfs_fs_drive, header.folder);
	
	fsop_deleteFolder(fname);
	return 0;
}

int DML_GameIsInstalled(char *folder)
{
	int ret = 0;
	char source[300];
	snprintf(source, sizeof(source), "sd:/games/%s/game.iso", folder);
	
	FILE *f = fopen(source, "rb");
	if(f) 
	{
		dbg_printf("Found on SD: %s\n", folder);
		fclose(f);
		ret = 1;
	}
	else
	{
		snprintf(source, sizeof(source), "sd:/games/%s/sys/boot.bin", folder);
		f = fopen(source, "rb");
		if(f) 
		{
			dbg_printf("Found on SD: %s\n", folder);
			fclose(f);
			ret = 2;
		}
	}
	
	if (ret == 0) {
		snprintf(source, sizeof(source), "%s/games/%s/game.iso", wbfs_fs_drive, folder);
	
		FILE *f = fopen(source, "rb");
		if(f) 
		{
			dbg_printf("Found on HDD: %s\n", folder);
			fclose(f);
			ret = 1;
		}
		else
		{
			snprintf(source, sizeof(source), "%s/games/%s/sys/boot.bin", wbfs_fs_drive, folder);
			f = fopen(source, "rb");
			if(f) 
			{
				dbg_printf("Found on HDD: %s\n", folder);
				fclose(f);
				ret = 2;
			}
		}
	}
	return ret;
}

void DML_New_SetOptions(char *GamePath, char *CheatPath, char *NewCheatPath, bool cheats, bool debugger, u8 NMM, u8 nodisc, u8 DMLvideoMode)
{
	dbg_printf("DML: Launch game 'sd:/games/%s/game.iso' through memory (new method)\n", GamePath);

	DML_CFG *DMLCfg = (DML_CFG*)memalign(32, sizeof(DML_CFG));
	memset(DMLCfg, 0, sizeof(DML_CFG));

	DMLCfg->Magicbytes = 0xD1050CF6;
	DMLCfg->CfgVersion = 0x00000001;
	//DMLCfg->VideoMode |= DML_VID_FORCE;
	DMLCfg->VideoMode |= DML_VID_DML_AUTO;

	DMLCfg->Config |= DML_CFG_ACTIVITY_LED; //Sorry but I like it lol, option will may follow
	DMLCfg->Config |= DML_CFG_PADHOOK; //Makes life easier, l+z+b+digital down...

	if(GamePath != NULL)
	{
		if(DML_GameIsInstalled(GamePath) == 2)
			snprintf(DMLCfg->GamePath, sizeof(DMLCfg->GamePath), "/games/%s/", GamePath);
		else
			snprintf(DMLCfg->GamePath, sizeof(DMLCfg->GamePath), "/games/%s/game.iso", GamePath);
		DMLCfg->Config |= DML_CFG_GAME_PATH;
	}

	if(CheatPath != NULL && NewCheatPath != NULL && cheats)
	{
		char *ptr;
		if(strstr(CheatPath, "sd:/") == NULL)
		{
			fsop_CopyFile(CheatPath, NewCheatPath);
			ptr = &NewCheatPath[3];
		}
		else
			ptr = &CheatPath[3];
		strncpy(DMLCfg->CheatPath, ptr, sizeof(DMLCfg->CheatPath));
		DMLCfg->Config |= DML_CFG_CHEAT_PATH;
	}

	if(cheats)
		DMLCfg->Config |= DML_CFG_CHEATS;
	if(debugger)
		DMLCfg->Config |= DML_CFG_DEBUGGER;
	if(NMM > 0)
		DMLCfg->Config |= DML_CFG_NMM;
	if(NMM > 1)
		DMLCfg->Config |= DML_CFG_NMM_DEBUG;
	if(nodisc > 0)
		DMLCfg->Config |= DML_CFG_NODISC;

	if(DMLvideoMode > 3)
		DMLCfg->VideoMode |= DML_VID_PROG_PATCH;


	//Write options into memory
	if (CFG.dml == CFG_DML_R52)
	{
		memcpy((void *)0xC0001700, DMLCfg, sizeof(DML_CFG));
	}
	else if (CFG.dml >= CFG_DML_1_2)
	{
		// For new DML v1.2+
		memcpy((void *)0xC1200000, DMLCfg, sizeof(DML_CFG));
	}
	
	free(DMLCfg);
}

void DML_Old_SetOptions(char *GamePath, char *CheatPath, char *NewCheatPath, bool cheats)
{
	dbg_printf("DML: Launch game 'sd:/games/%s/game.iso' through boot.bin (old method)\n", GamePath);
	FILE *f;
	f = fopen("sd:/games/boot.bin", "wb");
	fwrite(GamePath, 1, strlen(GamePath) + 1, f);
	fclose(f);

	if(cheats && strstr(CheatPath, NewCheatPath) == NULL)
		fsop_CopyFile(CheatPath, NewCheatPath);

	//Tell DML to boot the game from sd card
	*(vu32*)0x80001800 = 0xB002D105;
	DCFlushRange((void *)(0x80001800), 4);
	ICInvalidateRange((void *)(0x80001800), 4);

	*(vu32*)0xCC003024 |= 7;
}

void DML_New_SetBootDiscOption()
{
	dbg_printf("Booting GC game\n");

	DML_CFG *DMLCfg = (DML_CFG*)malloc(sizeof(DML_CFG));
	memset(DMLCfg, 0, sizeof(DML_CFG));

	DMLCfg->Magicbytes = 0xD1050CF6;
	DMLCfg->CfgVersion = 0x00000001;

	DMLCfg->Config |= DML_CFG_BOOT_DISC;

	//DML v1.2+
	memcpy((void *)0xC1200000, DMLCfg, sizeof(DML_CFG));

	free(DMLCfg);
}

s32 DML_write_size_info_file(struct discHdr *header, u64 size) {
	char filepath[0xFF];
	FILE *infoFile = NULL;
	
	if (header->magic == DML_MAGIC) {
		snprintf(filepath, sizeof(filepath), "sd:/games/%s/size.bin", header->folder);
	} else if (header->magic == DML_MAGIC_HDD) {
		snprintf(filepath, sizeof(filepath), "%s/games/%s/size.bin", wbfs_fs_drive, header->folder);
	}
	
	infoFile = fopen(filepath, "wb");
	fwrite(&size, 1, sizeof(u64), infoFile);
	fclose(infoFile);
	return 0;
}

u64 DML_read_size_info_file(struct discHdr *header) {
	char filepath[0xFF];
	FILE *infoFile = NULL;
	u64 result = 0;
	
	if (header->magic == DML_MAGIC) {
		snprintf(filepath, sizeof(filepath), "sd:/games/%s/size.bin", header->folder);
	} else if (header->magic == DML_MAGIC_HDD) {
		snprintf(filepath, sizeof(filepath), "%s/games/%s/size.bin", wbfs_fs_drive, header->folder);
	}
	
	infoFile = fopen(filepath, "rb");
	if (infoFile) {
		fread(&result, 1, sizeof(u64), infoFile);
		fclose(infoFile);
	}
	return result;
}

u64 getDMLGameSize(struct discHdr *header) {
	u64 result = 0;
	if (header->magic == DML_MAGIC)
	{
		char filepath[0xFF];
		snprintf(filepath, sizeof(filepath), "sd:/games/%s/game.iso", header->folder);
		FILE *fp = fopen(filepath, "r");
		if (!fp)
		{
			snprintf(filepath, sizeof(filepath), "sd:/games/%s/sys/boot.bin", header->folder);
			FILE *fp = fopen(filepath, "r");
			if (!fp)
				return result;
			fclose(fp);
			result = DML_read_size_info_file(header);
			if (result > 0)
				return result;
			snprintf(filepath, sizeof(filepath), "sd:/games/%s/root/", header->folder);
			result = fsop_GetFolderBytes(filepath);
			if (result > 0)
				DML_write_size_info_file(header, result);
		}
		else
		{
			fseek(fp, 0, SEEK_END);
			result = ftell(fp);
			fclose(fp);
		}
		return result;
	}
	else if (header->magic == DML_MAGIC_HDD) 
	{
		char filepath[0xFF];
		sprintf(filepath, "%s/games/%s/game.iso", wbfs_fs_drive, header->folder);
		FILE *fp = fopen(filepath, "r");
		if (!fp)
		{
			snprintf(filepath, sizeof(filepath), "%s/games/%s/sys/boot.bin", wbfs_fs_drive, header->folder);
			FILE *fp = fopen(filepath, "r");
			if (!fp)
				return result;
			fclose(fp);
			result = DML_read_size_info_file(header);
			if (result > 0)
				return result;
			snprintf(filepath, sizeof(filepath), "%s/games/%s/root/", wbfs_fs_drive, header->folder);
			result = fsop_GetFolderBytes(filepath);
			if (result > 0)
				DML_write_size_info_file(header, result);
		}
		else
		{
			fseek(fp, 0, SEEK_END);
			result = ftell(fp);
			fclose(fp);
		}
		return result;
	}
	return result;
}

s32 delete_Old_Copied_DML_Game() {
	FILE *infoFile = NULL;
	struct discHdr header;
	infoFile = fopen("sd:/games/lastCopied.bin", "rb");
	if (infoFile) {
		fread(&header, 1, sizeof(struct discHdr), infoFile);
		fclose(infoFile);
		DML_RemoveGame(header, true);
		return 0;
	}
	return -1;
}

s32 copy_DML_Game_to_SD(struct discHdr *header) {
	char source[255];
	char target[255];
	sprintf(source, "%s/games/%s", wbfs_fs_drive, header->folder);
	sprintf(target, "sd:/games/%s", header->folder);
	fsop_CopyFolder(source, target);
	header->magic = DML_MAGIC;
	
	FILE *infoFile = NULL;
	infoFile = fopen("sd:/games/lastCopied.bin", "wb");
	fwrite((u8*)header, 1, sizeof(struct discHdr), infoFile);
	fclose(infoFile);
	return 0;
}