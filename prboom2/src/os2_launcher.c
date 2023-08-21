/* Emacs style mode select   -*- C++ -*-
 *-----------------------------------------------------------------------------
 *
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2000 by
 *  Jess Haas, Nicolas Kalkhof, Colin Phipps, Florian Schulze
 *  Copyright 2005, 2006 by
 *  Florian Schulze, Colin Phipps, Neil Stevens, Andrey Budko
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *  02111-1307, USA.
 *
 * DESCRIPTION:
 *
 *-----------------------------------------------------------------------------
 */

#ifdef OS2
#define INCL_WINSYS
#define INCL_WINFRAMEMGR
#define INCL_WINDIALOGS
#define INCL_DOSFILEMGR
#define INCL_DOSERRORS
#define INCL_WININPUT
#define INCL_WINERRORS
#define INCL_WINLISTBOXES
#include <os2.h>

#include <stddef.h>
#include <libgen.h>
#include "i_glob.h"

#include "doomtype.h"
#include "w_wad.h"
#include "doomstat.h"
#include "lprintf.h"
#include "d_main.h"
#include "m_misc.h"
#include "i_system.h"
#include "m_argv.h"
#include "i_main.h"
#include ".\..\ICONS\resource.h"
#ifdef HAVE_LIBPCREPOSIX
#include "pcreposix.h"
#endif /* HAVE_LIBPCREPOSIX */
#include "r_demo.h"
#include "e6y.h"
#include "os2_launcher.h"

#include "m_io.h"

                                        /* Define parameters by type    */
HAB   hab;                              /* Anchor block handle          */
HMQ   hmq;                              /* Message queue handle         */
HWND  hwndClient;                       /* Client window handle         */
ULONG flCreate = FCF_STANDARD;          /* Frame control flag           */
BOOL  bComplete = FALSE;                /* Switch for first time through*/
HWND  hwndFrame;                        /* Frame window handle          */

#define ETDT_ENABLE         0x00000002
#define ETDT_USETABTEXTURE  0x00000004
#define ETDT_ENABLETAB      (ETDT_ENABLE  | ETDT_USETABTEXTURE)

#define LAUNCHER_HISTORY_SIZE 10

#define LAUNCHER_CAPTION PACKAGE_NAME" Launcher"

#define I_FindName(a)	((a)->FindBuffer.achName)
#define I_FindAttr(a)	((a)->FindBuffer.attrFile)

typedef struct
{
  char name[PATH_MAX];
  wad_source_t source;
  BOOL doom1;
  BOOL doom2;
} fileitem_t;

typedef struct
{
  HWND HWNDLauncher;
  HWND listIWAD;
  HWND listPWAD;
  HWND listHistory;
  HWND listCMD;
  HWND staticFileName;
  
  fileitem_t *files;
  size_t filescount;
  
  fileitem_t *cache;
  size_t cachesize;
  
  int *selection;
  size_t selectioncount;
} launcher_t;

launcher_t launcher;

launcher_enable_t launcher_enable;
const char *launcher_enable_states[launcher_enable_count] = {"never", "smart", "always"};
char *launcher_history[LAUNCHER_HISTORY_SIZE];

static char launchercachefile[PATH_MAX];

unsigned int launcher_params;

extern const int nstandard_iwads;
extern const char *const standard_iwads[];


//global
void CheckIWAD(const char *iwadname,GameMode_t *gmode, dboolean *hassec);
void ProcessDehFile(const char *filename, const char *outfilename, int lumpnum);
const char *D_dehout(void);


//events
static void L_GameOnChange(void);

static void L_FillFilesList(fileitem_t *iwad);
static void L_FillHistoryList(void);


//selection
static void L_SelAdd(int index);
static void L_SelClearAndFree(void);
static int L_SelGetList(int **list);

static dboolean L_GUISelect(waddata_t *waddata);


#define prb_isspace(c) ((c) == 0x20)
char *strrtrm (char *str)
{
  if (str)
  {
    char *p = str + strlen (str)-1;
    while (p >= str && prb_isspace((unsigned char) *p))
      p--;
    *++p = 0;
  }
  return str;
}
#undef prb_isspace

static void L_AddItemToCache(fileitem_t *item)
{
  FILE *fcache;

  if ( (fcache = M_fopen(launchercachefile, "at")) )
  {
    fprintf(fcache, "%s = %d, %d, %d\n",item->name, item->source, item->doom1, item->doom2);
    fclose(fcache);
  }
}

static ULONG IsIWADName(const char *name)
{
    int i;
    char *filename = PathFindFileName(name);

    for (i = 0; i < nstandard_iwads; i++)
    {
        if (!strcasecmp(filename, standard_iwads[i]))
        {
            return true;
        }
    }

    return false;
}

static ULONG L_GetFileType(const char *filename, fileitem_t *item)
{
  size_t i, len;
  wadinfo_t header;
  FILE *f;

  item->source = source_err;
  item->doom1 = false;
  item->doom2 = false;
  strcpy(item->name, filename);
  
  len = strlen(filename);

  if (!strcasecmp(&filename[len-4],".deh") || !strcasecmp(&filename[len-4],".bex"))
  {
    item->source = source_deh;
    return true;
  }
  
  for (i = 0; i < launcher.cachesize; i++)
  {
    if (!strcasecmp(filename, launcher.cache[i].name))
    {
      strcpy(item->name, launcher.cache[i].name);
      item->source = launcher.cache[i].source;
      item->doom1 = launcher.cache[i].doom1;
      item->doom2 = launcher.cache[i].doom2;
      return true;
    }
  }

  if ( (f = M_fopen (filename, "rb")) )
  {
    fread (&header, sizeof(header), 1, f);
    if (!strncmp(header.identification, "IWAD", 4) ||
        (!strncmp(header.identification, "PWAD", 4) && IsIWADName(filename)))
    {
      item->source = source_iwad;
    }
    else if (!strncmp(header.identification, "PWAD", 4))
    {
      item->source = source_pwad;
    }
    if (item->source != source_err)
    {
      header.numlumps = LittleLong(header.numlumps);
      if (0 == fseek(f, LittleLong(header.infotableofs), SEEK_SET))
      {
        for (i = 0; !item->doom1 && !item->doom2 && i < (size_t)header.numlumps; i++)
        {
          filelump_t lump;
          
          if (0 == fread (&lump, sizeof(lump), 1, f))
            break;

          if (strlen(lump.name) == 4)
          {
            if ((lump.name[0] == 'E' && lump.name[2] == 'M') &&
              (lump.name[1] >= '1' && lump.name[1] <= '4') &&
              (lump.name[3] >= '1' && lump.name[3] <= '9'))
              item->doom1 = true;
          }

          if (strlen(lump.name) == 5)
          {
            if (!strncmp(lump.name, "MAP", 3) &&
              (lump.name[3] >= '0' && lump.name[3] <= '9') &&
              (lump.name[4] >= '0' && lump.name[4] <= '9'))
              item->doom2 = true;
          }

        }
        L_AddItemToCache(item);
      }
    }
    fclose(f);
    return true;
  }
  return false;
}

char* e6y_I_FindFile(const char* ext)
{
  int i;
  /* Precalculate a length we will need in the loop */
  size_t  pl = strlen(ext) + 4;
  glob_t *globdata;

  for (i=0; i<3; i++) {
    //char  * p;
    char d[PATH_MAX];
    
    strcpy(d, "");
    switch(i) {
      case 0:
        M_getcwd(d, sizeof(d));
        break;
      case 1:
        if (!M_getenv("DOOMWADDIR"))
          continue;
        strcpy(d, M_getenv("DOOMWADDIR"));
        break;
      case 2:
        strcpy(d, I_DoomExeDir());
        break;
    }

    //p = malloc(strlen(d) + (s ? strlen(s) : 0) + pl);
    //strcpy(p, d);
/*    sprintf(p, "%s%s%s%s", d, (d && !HasTrailingSlash(d)) ? "/" : "",
                             s ? s : "", (s && !HasTrailingSlash(s)) ? "/" : "");*/
    globdata = I_StartGlob(d, ext, GLOB_FLAG_NOCASE);
    if (globdata != NULL) {
      char *fullpath;
      fileitem_t item;
      
      while(fullpath = I_NextGlob(globdata))
      {
        if (L_GetFileType(fullpath, &item))
        {
          if (item.source != source_err)
          {
            size_t j;
            dboolean present = false;
            for (j = 0; !present && j < launcher.filescount; j++)
              present = !strcasecmp(launcher.files[j].name, fullpath);
            if (!present)
            {
              launcher.files = realloc(launcher.files, sizeof(*launcher.files) * (launcher.filescount + 1));
              
              strcpy(launcher.files[launcher.filescount].name, fullpath);
              launcher.files[launcher.filescount].source = item.source;
              launcher.files[launcher.filescount].doom1 = item.doom1;
              launcher.files[launcher.filescount].doom2 = item.doom2;
              launcher.filescount++;
            }
          }
        }
      }
      
      I_EndGlob(globdata);
    }

    //free(p);
  }
  return NULL;
}

static void L_CommandOnChange(void)
{
  int index;

  index = SHORT1FROMMR(WinSendMsg(launcher.listCMD, LM_QUERYSELECTION, MPFROMSHORT(0), MPFROMSHORT(0)));
  
  switch (index)
  {
  case 0:
    M_remove(launchercachefile);
    
    WinSendMsg(launcher.listPWAD, LM_DELETEALL, MPFROMSHORT(0), MPFROMSHORT(0));
    WinSendMsg(launcher.listHistory, LM_SELECTITEM, MPFROMSHORT(LIT_NONE), MPFROMSHORT(0));
    
    if (launcher.files)
    {
      free(launcher.files);
      launcher.files = NULL;
    }
    launcher.filescount = 0;

    if (launcher.cache)
    {
      free(launcher.cache);
      launcher.cache = NULL;
    }
    launcher.cachesize = 0;

    e6y_I_FindFile("*.wad");
    e6y_I_FindFile("*.deh");
    e6y_I_FindFile("*.bex");

    L_GameOnChange();

    WinMessageBox(HWND_DESKTOP, launcher.HWNDLauncher, (PCSZ)"The cache has been successfully rebuilt", 
        (PCSZ)LAUNCHER_CAPTION, 0, MB_OK|MB_ICONEXCLAMATION);
    break;
  case 1:
    {
      size_t i;
      for (i = 0; i < sizeof(launcher_history)/sizeof(launcher_history[0]); i++)
      {
        char str[32];
        default_t *history;

        sprintf(str, "launcher_history%d", i);
        history = M_LookupDefault(str);
        
        strcpy(history->location.ppsz[0], "");
      }
      M_SaveDefaults();
      L_FillHistoryList();
      WinSendMsg(launcher.listHistory, LM_SELECTITEM, MPFROMSHORT(LIT_NONE), MPFROMSHORT(0));
  
      WinMessageBox(HWND_DESKTOP, launcher.HWNDLauncher, (PCSZ)"The history has been successfully cleared", 
          (PCSZ)LAUNCHER_CAPTION, 0, MB_OK|MB_ICONEXCLAMATION);
    }
    break;

  case 2:
  case 3:
  case 4:
    {
      /* TODO
      DWORD result;
      char *msg;
      char *cmdline;

      cmdline = malloc(strlen(*myargv) + 100);

      if (cmdline)
      {
        sprintf(cmdline, "\"%s\" \"%%1\"", *myargv);

        result = 0;
        if (index == 2)
          result = L_Associate("PrBoomPlusWadFiles", ".wad", cmdline);
        if (index == 3)
          result = L_Associate("PrBoomPlusLmpFiles", ".lmp", cmdline);
        if (index == 4)
        {
          strcat(cmdline, " -auto");
          result = L_Associate("PrBoomPlusLmpFiles", ".lmp", cmdline);
        }

        free(cmdline);

        if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
          NULL, result, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (char *)&msg, 512, NULL))
        {
          WinMessageBox(HWND_DESKTOP, launcher.HWNDLauncher, (PCSZ)msg, (PCSZ)LAUNCHER_CAPTION,
            0, MB_OK | (result == NO_ERROR ? MB_ICONASTERISK : MB_ICONEXCLAMATION));
          LocalFree(msg);
        }
      } */
    }
    break;

  case 5:
    {
      char buf[256], next_mode[100];
      launcher_enable_t launcher_next_mode = (launcher_enable + 1) % launcher_enable_count;
      
      if (launcher_next_mode == launcher_enable_never)
        strcpy(next_mode, "disable");
      if (launcher_next_mode == launcher_enable_smart)
        strcpy(next_mode, "enable ('smart' mode)");
      if (launcher_next_mode == launcher_enable_always)
        strcpy(next_mode, "enable ('always' mode)");

      sprintf(buf, "Do you really want to %s the Launcher?", next_mode);
      if (WinMessageBox(HWND_DESKTOP, launcher.HWNDLauncher, (PCSZ)buf, (PCSZ)LAUNCHER_CAPTION, 
          0, MB_YESNO|MB_ICONQUESTION) == MBID_YES)
      {
        launcher_enable = launcher_next_mode;

// TODO Replace
        WinSendMsg(launcher.listCMD, LM_DELETEITEM, MPFROMSHORT(index), MPFROMSHORT(0));
        strcpy(buf, ((launcher_enable + 1) % launcher_enable_count == launcher_enable_never ? "Disable" : "Enable"));
        strcat(buf, " this Launcher for future use");
        WinSendMsg(launcher.listCMD, LM_INSERTITEM, MPFROMSHORT(index), MPFROMP(buf));

        M_SaveDefaults();
        sprintf(buf, "Successfully %s", (launcher_enable != launcher_enable_never ? "enabled" : "disabled"));
        WinMessageBox(HWND_DESKTOP, launcher.HWNDLauncher, (PCSZ)buf, (PCSZ)LAUNCHER_CAPTION, 
            0, MB_OK|MB_ICONEXCLAMATION);
      }
    }
    break;
  }
  
  WinSendMsg(launcher.listCMD, LM_SELECTITEM, MPFROMSHORT(LIT_NONE), MPFROMSHORT(0));
}

//events
static void L_GameOnChange(void)
{
  int index;

  index = SHORT1FROMMR(WinSendMsg(launcher.listIWAD, LM_QUERYSELECTION, MPFROMSHORT(0), MPFROMSHORT(0)));
  if (index != LIT_NONE)
  {
    index = SHORT1FROMMR(WinSendMsg(launcher.listIWAD, LM_QUERYITEMHANDLE, MPFROMSHORT(index), MPFROMSHORT(0)));
    if (index != LIT_NONE)
    {
      L_FillFilesList(&launcher.files[index]);
    }
  }
}

static void L_FilesOnChange(void)
{
  int index;
  int i;

  i = SHORT1FROMMR(WinSendMsg(launcher.listPWAD, LM_QUERYSELECTION, MPFROMSHORT(LIT_FIRST), MPFROMSHORT(0)));

  do
  {
    index = SHORT1FROMMR(WinSendMsg(launcher.listPWAD, LM_QUERYITEMHANDLE, MPFROMSHORT(i), MPFROMSHORT(0)));
    if (index == NO_ERROR)
    {
      L_SelAdd(index);
    }
    i = SHORT1FROMMR(WinSendMsg(launcher.listPWAD, LM_QUERYSELECTION, MPFROMSHORT(i), MPFROMSHORT(0)));
  } while(i == NO_ERROR);
  
  index = SHORT1FROMMR(WinSendMsg(launcher.listPWAD, LM_QUERYITEMHANDLE, MPFROMSHORT(index), MPFROMSHORT(0)));
  if (index == NO_ERROR)
  {
    char path[PATH_MAX];
    size_t count;

    // TODO Query font space for string dependent on font.
    strcpy(path, launcher.files[index].name);
    NormalizeSlashes2(path);
    M_Strlwr(path);

    // TODO WinSendMsg(launcher.staticFileName, WM_SETTEXT, 0, (LPARAM)path);
  }
}

static void L_HistoryOnChange(void)
{
  int index;

  index = SHORT1FROMMR(WinSendMsg(launcher.listHistory, LM_QUERYSELECTION, MPFROMSHORT(0), MPFROMSHORT(0)));
  if (index >= 0)
  {
    waddata_t *waddata;
    waddata = (waddata_t*)PVOIDFROMMR(WinSendMsg(launcher.listHistory, LM_QUERYITEMHANDLE, MPFROMSHORT(index), MPFROMSHORT(0)));
    if (waddata != NULL)
    {
      if (!L_GUISelect(waddata))
      {
        WinSendMsg(launcher.listHistory, LM_SELECTITEM, MPFROMSHORT(LIT_NONE), MPFROMSHORT(0));
      }
    }
  }
}

/*
static DWORD L_Associate(const char *Name, const char *Ext, const char *cmdline)
{
  HKEY hKeyRoot, hKey;
  DWORD result = 0;

  hKeyRoot = HKEY_CLASSES_ROOT;

  // This creates a Root entry called 'Name'
  result = RegCreateKey(hKeyRoot, Name, &hKey);
  if (result != ERROR_SUCCESS) return result;
  result = RegSetValue(hKey, "", REG_SZ, "PrBoom-Plus", 0);
  if (result != ERROR_SUCCESS) return result;
  RegCloseKey(hKey);

  // This creates a Root entry called 'Ext' associated with 'Name'
  result = RegCreateKey(hKeyRoot, Ext, &hKey);
  if (result != ERROR_SUCCESS) return result;
  result = RegSetValue(hKey, "", REG_SZ, Name, 0);
  if (result != ERROR_SUCCESS) return result;
  RegCloseKey(hKey);

  // This sets the command line for 'Name'
  result = RegCreateKey(hKeyRoot, Name, &hKey);
  if (result != ERROR_SUCCESS) return result;
  result = RegSetValue(hKey, "shell\\open\\command", REG_SZ, cmdline, strlen(cmdline) + 1);
  if (result != ERROR_SUCCESS) return result;
  RegCloseKey(hKey);

  return result;
}
*/


static dboolean L_GUISelect(waddata_t *waddata)
{
  int i, j;
  size_t k;
  int topindex;
  dboolean processed = false;
  int listIWADCount, listPWADCount;
  char fullpath[PATH_MAX];
  
  if (!waddata->wadfiles)
    return false;

  listIWADCount = SHORT1FROMMR(WinSendMsg(launcher.listIWAD, LM_QUERYITEMCOUNT, MPFROMSHORT(0), MPFROMSHORT(0)));
  WinSendMsg(launcher.listIWAD, LM_SELECTITEM, MPFROMSHORT(LIT_NONE), MPFROMSHORT(0));

  for (k=0; !processed && k < waddata->numwadfiles; k++)
  {
    if (DosQueryPathInfo(waddata->wadfiles[k].name, FIL_QUERYFULLNAME, fullpath, PATH_MAX) == NO_ERROR)
    {
      switch (waddata->wadfiles[k].src)
      {
      case source_iwad:
        for (i=0; !processed && (size_t)i<launcher.filescount; i++)
        {
          if (launcher.files[i].source == source_iwad &&
              !strcasecmp(launcher.files[i].name, fullpath))
          {
            for (j=0; !processed && j < listIWADCount; j++)
            {
              if (SHORT1FROMMR(WinSendMsg(launcher.listIWAD, LM_QUERYITEMHANDLE, MPFROMSHORT(j), MPFROMSHORT(0))) == i)
              {
                if (SHORT1FROMMR(WinSendMsg(launcher.listIWAD, LM_SELECTITEM, MPFROMSHORT(j), MPFROMSHORT(0))) != 0)
                {
                  processed = true;
                  L_GameOnChange();
                }
              }
            }
          }
        }
        break;
      }
    }
  }

  //no iwad?
  if (!processed)
    return false;

  listPWADCount = SHORT1FROMMR(WinSendMsg(launcher.listPWAD, LM_QUERYITEMCOUNT, MPFROMSHORT(0), MPFROMSHORT(0)));
  for (i = 0; i < listPWADCount; i++)
    WinSendMsg(launcher.listPWAD, LM_SELECTITEM, MPFROMSHORT(i), MPFROMSHORT(0));

  topindex = -1;

  for (k=0; k < waddata->numwadfiles; k++)
  {
    if (DosQueryPathInfo(waddata->wadfiles[k].name, FIL_QUERYFULLNAME, fullpath, PATH_MAX) == NO_ERROR)
    {
      switch (waddata->wadfiles[k].src)
      {
      case source_deh:
      case source_pwad:
        processed = false;
        for (j=0; !processed && j < listPWADCount; j++)
        {
          int index = SHORT1FROMMR(WinSendMsg(launcher.listPWAD, LM_QUERYITEMHANDLE, MPFROMSHORT(j), MPFROMSHORT(0)));
          if (index != 0)
          {
            if (!strcasecmp(launcher.files[index].name, fullpath))
              if (WinSendMsg(launcher.listPWAD, LM_SELECTITEM, MPFROMSHORT(j), MPFROMSHORT(1)) != 0)
              {
                if (topindex == -1)
                  topindex = j;
                L_SelAdd(index);
                processed = true;
              }
          }
        }
        if (!processed)
          return false;
        break;
      }
    }
    //else
    //  return false;
  }
  
  if (topindex == -1)
    topindex = 0;
  WinSendMsg(launcher.listPWAD, LM_SETTOPINDEX, MPFROMSHORT(topindex), MPFROMSHORT(0));

  return true;
}

static ULONG L_PrepareToLaunch(void)
{
  int i, index, listPWADCount;
  char *history = NULL;
  wadfile_info_t *new_wadfiles=NULL;
  size_t new_numwadfiles = 0;
  int *selection = NULL;
  int selectioncount = 0;

  new_numwadfiles = numwadfiles;
  new_wadfiles = malloc(sizeof(*wadfiles) * numwadfiles);
  memcpy(new_wadfiles, wadfiles, sizeof(*wadfiles) * numwadfiles);
  numwadfiles = 0;
  free(wadfiles);
  wadfiles = NULL;
  
  listPWADCount = SHORT1FROMMR(WinSendMsg(launcher.listPWAD, LM_QUERYITEMCOUNT, MPFROMSHORT(0), MPFROMSHORT(0)));
  
  index = SHORT1FROMMR(WinSendMsg(launcher.listIWAD, LM_QUERYSELECTION, MPFROMSHORT(0), MPFROMSHORT(0)));
  if (index != 0)
  {
    index = SHORT1FROMMR(WinSendMsg(launcher.listIWAD, LM_QUERYITEMHANDLE, MPFROMSHORT(index), MPFROMSHORT(0)));
    if (index != 0)
    {
      extern void D_AutoloadIWadDir();
      char *iwadname = PathFindFileName(launcher.files[index].name);
      history = malloc(strlen(iwadname) + 8);
      strcpy(history, iwadname);
      AddIWAD(launcher.files[index].name);
      D_AutoloadIWadDir();
    }
  }

  if (numwadfiles == 0)
    return false;

  for (i = 0; (size_t)i < new_numwadfiles; i++)
  {
    if (new_wadfiles[i].src == source_auto_load || new_wadfiles[i].src == source_pre)
    {
      wadfiles = realloc(wadfiles, sizeof(*wadfiles)*(numwadfiles+1));
      wadfiles[numwadfiles].name = strdup(new_wadfiles[i].name);
      wadfiles[numwadfiles].src = new_wadfiles[i].src;
      wadfiles[numwadfiles].handle = new_wadfiles[i].handle;
      numwadfiles++;
    }
  }

  selectioncount = L_SelGetList(&selection);

  for (i=0; i < selectioncount; i++)
  {
    int index = selection[i];
    fileitem_t *item = &launcher.files[index];

    if (item->source == source_pwad || item->source == source_iwad)
    {
      D_AddFile(item->name, source_pwad);
      modifiedgame = true;
    }

    if (item->source == source_deh)
      ProcessDehFile(item->name, D_dehout(),0);

    history = realloc(history, strlen(history) + strlen(item->name) + 8);
    strcat(history, "|");
    strcat(history, item->name);
  }
 
  free(selection);
  L_SelClearAndFree();

  for (i = 0; (size_t)i < new_numwadfiles; i++)
  {
    if (new_wadfiles[i].src == source_lmp || new_wadfiles[i].src == source_net)
      D_AddFile(new_wadfiles[i].name, new_wadfiles[i].src);
    if (new_wadfiles[i].name)
      free((char*)new_wadfiles[i].name);
  }
  free(new_wadfiles);

  if (history)
  {
    size_t i;
    char str[32];
    default_t *history1, *history2;
    size_t historycount = sizeof(launcher_history)/sizeof(launcher_history[0]);
    size_t shiftfrom = historycount - 1;

    for (i = 0; i < historycount; i++)
    {
      sprintf(str, "launcher_history%d", i);
      history1 = M_LookupDefault(str);

      if (!strcasecmp(history1->location.ppsz[0], history))
      {
        shiftfrom = i;
        break;
      }
    }

    for (i = shiftfrom; i > 0; i--)
    {
      sprintf(str, "launcher_history%d", i);
      history1 = M_LookupDefault(str);
      sprintf(str, "launcher_history%d", i-1);
      history2 = M_LookupDefault(str);

      if (i == shiftfrom)
        free((char*)history1->location.ppsz[0]);
      history1->location.ppsz[0] = history2->location.ppsz[0];
    }
    if (shiftfrom > 0)
    {
      history1 = M_LookupDefault("launcher_history0");
      history1->location.ppsz[0] = history;
    }
  }
  return 1;
}

static void L_ReadCacheData(void)
{
  FILE *fcache;
printf("cachefile %s\n", launchercachefile);
  if ( (fcache = M_fopen(launchercachefile, "rt")) )
  {
    fileitem_t item;
    char name[PATH_MAX];

    while (fgets(name, sizeof(name), fcache))
    {
      char *p = strrchr(name, '=');
      if (p)
      {
        *p = 0;
        if (3 == sscanf(p + 1, "%d, %d, %d", &item.source, &item.doom1, &item.doom2))
        {
          launcher.cache = realloc(launcher.cache, sizeof(*launcher.cache) * (launcher.cachesize + 1));
          strcpy(launcher.cache[launcher.cachesize].name, M_Strlwr(strrtrm(name)));
          launcher.cache[launcher.cachesize].source = item.source;
          launcher.cache[launcher.cachesize].doom1 = item.doom1;
          launcher.cache[launcher.cachesize].doom2 = item.doom2;
          launcher.cachesize++;
        }
      }
    }
    fclose(fcache);
  }
}

static void L_SelAdd(int index)
{
  launcher.selection = realloc(launcher.selection, 
    sizeof(launcher.selection[0]) * (launcher.selectioncount + 1));
  launcher.selection[launcher.selectioncount] = index;
  launcher.selectioncount++;
}

static void L_SelClearAndFree(void)
{
  free(launcher.selection);
  launcher.selection = NULL;
  launcher.selectioncount = 0;
}

static int L_SelGetList(int **list)
{
  int i, j, count = 0;
  int listPWADCount = SHORT1FROMMR(WinSendMsg(launcher.listPWAD, LM_QUERYITEMCOUNT, MPFROMSHORT(0), MPFROMSHORT(0)));

  *list = NULL;

  for (i = launcher.selectioncount - 1; i >= 0; i--)
  {
    dboolean present = false;
    for (j = 0; j < count && !present; j++)
    {
      present = (*list)[j] == launcher.selection[i];
    }
    
    if (!present)
    {
      for (j=0; j < listPWADCount; j++)
      {
        int index = launcher.selection[i];
        if (SHORT1FROMMR(WinSendMsg(launcher.listPWAD, LM_QUERYITEMHANDLE, MPFROMSHORT(j), MPFROMSHORT(0))) == index)
        {
          if (SHORT1FROMMR(WinSendMsg(launcher.listPWAD, LM_QUERYSELECTION, MPFROMSHORT(j), MPFROMSHORT(0))) != LIT_NONE)
          {
            *list = realloc(*list, sizeof(int) * (count + 1));
            (*list)[count++] = launcher.selection[i];
          }
        }
      }
    }
  }

  for (i = 0; i < count / 2; i++)
  {
    int tmp = (*list)[i];
    (*list)[i] = (*list)[count - 1 - i];
    (*list)[count - 1 - i] = tmp;
  }

  return count;
}



static void L_FillGameList(void)
{
  int i, j;
  
  // "doom2f.wad", "doom2.wad", "plutonia.wad", "tnt.wad",
  // "doom.wad", "doom1.wad", "doomu.wad",
  // "freedoom2.wad", "freedoom1.wad", "freedm.wad"
  // "hacx.wad", "chex.wad"
  // "bfgdoom2.wad", "bfgdoom.wad"
  const char *IWADTypeNames[] =
  {
    "DOOM 2: French Version",
    "DOOM 2: Hell on Earth",
    "DOOM 2: Plutonia Experiment",
    "DOOM 2: TNT - Evilution",

    "DOOM Registered",
    "DOOM Shareware",
    "The Ultimate DOOM",

    "Freedoom: Phase 2",
    "Freedoom: Phase 1",
    "FreeDM",

    "HACX - Twitch 'n Kill",
    "Chex(R) Quest",
    "REKKR",

    "DOOM 2: BFG Edition",
    "DOOM 1: BFG Edition",
  };
  
  
  for (i = 0; (size_t)i < launcher.filescount; i++)
  {
    fileitem_t *item = &launcher.files[i];
    if (item->source == source_iwad)
    {
      for (j=0; j < nstandard_iwads; j++)
      {
        if (!strcasecmp(basename(item->name), standard_iwads[j]))
        {
          char iwadname[128];
          int index;
          sprintf(iwadname, "%s (%s)", IWADTypeNames[j], standard_iwads[j]);
          index = SHORT1FROMMR(WinSendMsg(launcher.listIWAD, LM_INSERTITEM, MPFROMSHORT(LIT_END), MPFROMP((PSZ)iwadname)));
printf("Index %d (%d, %d)\n", index, LIT_MEMERROR, LIT_ERROR);
          //if (index >= 0)
            //WinSendMsg(launcher.listIWAD, LM_SETITEMHANDLE, MPFROMSHORT(index), MPFROMSHORT(i));
        }
      }
    }
  }
}

static void L_FillFilesList(fileitem_t *iwad)
{
  int index;
  size_t i;
  fileitem_t *item;

  WinSendMsg(launcher.listPWAD, LM_DELETEALL, MPFROMSHORT(0), MPFROMSHORT(0));

  for (i = 0; i < launcher.filescount; i++)
  {
    item = &launcher.files[i];
    if ((iwad->doom1 && item->doom1) || (iwad->doom2 && item->doom2) ||
      (!item->doom1 && !item->doom2) ||
      item->source == source_deh)
    {
      index = SHORT1FROMMR(WinSendMsg(launcher.listPWAD, LM_INSERTITEM, MPFROMSHORT(0), 
          MPFROMP(M_Strlwr(PathFindFileName(item->name)))));
      if (index >= 0)
      {
        WinSendMsg(launcher.listPWAD, LM_SETITEMHANDLE, MPFROMSHORT(index), MPFROMSHORT(i));
      }
    }
  }
}


static char* L_HistoryGetStr(waddata_t *data)
{
  size_t i;
  char *iwad = NULL;
  char *pwad = NULL;
  char *deh = NULL;
  char **str;
  char *result;
  size_t len;

  for (i = 0; i < data->numwadfiles; i++)
  {
    str = NULL;
    switch (data->wadfiles[i].src)
    {
    case source_iwad: str = &iwad; break;
    case source_pwad: str = &pwad; break;
    case source_deh:  str = &deh;  break;
    }
    if (*str)
    {
      *str = realloc(*str, strlen(*str) + strlen(data->wadfiles[i].name) + 8);
      strcat(*str, " + ");
      strcat(*str, PathFindFileName(data->wadfiles[i].name));
    }
    else
    {
      *str = malloc(strlen(data->wadfiles[i].name) + 8);
      strcpy(*str, PathFindFileName(data->wadfiles[i].name));
    }
  }

  len = 0;
  if (iwad) len += strlen(iwad);
  if (pwad) len += strlen(pwad);
  if (deh)  len += strlen(deh);
  
  result = malloc(len + 16);
  strcpy(result, "");
  
  if (pwad)
  {
    strcat(result, M_Strlwr(pwad));
    if (deh)
      strcat(result, " + ");
    free(pwad);
  }
  if (deh)
  {
    strcat(result, M_Strlwr(deh));
    free(deh);
  }
  if (iwad)
  {
    strcat(result, " @ ");
    strcat(result, M_Strupr(iwad));
    free(iwad);
  }

  return result;
}

static void L_HistoryFreeData(void)
{
  int i, count;
  count = SHORT1FROMMR(WinSendMsg(launcher.listHistory, LM_QUERYITEMCOUNT, MPFROMSHORT(0), MPFROMSHORT(0)));
  if (count != 0)
  {
    for (i = 0; i < count; i++)
    {
      waddata_t *waddata = (waddata_t*)PVOIDFROMMR(WinSendMsg(launcher.listHistory, LM_QUERYITEMHANDLE, MPFROMSHORT(i), MPFROMSHORT(0)));
      if (waddata != NULL)
      {
        WadDataFree(waddata);
      }
    }
  }
}

static void L_FillHistoryList(void)
{
  int i;
  char *p = NULL;

  L_HistoryFreeData();
  
  WinSendMsg(launcher.listHistory, LM_DELETEALL, MPFROMSHORT(0), MPFROMSHORT(0));

  for (i = 0; i < sizeof(launcher_history)/sizeof(launcher_history[0]); i++)
  {
    if (strlen(launcher_history[i]) > 0)
    {
      int index;
      char *str = strdup(launcher_history[i]);
      waddata_t *waddata = malloc(sizeof(*waddata));
      memset(waddata, 0, sizeof(*waddata));

      ParseDemoPattern(str, waddata, NULL, false);
      p = L_HistoryGetStr(waddata);

      if (p)
      {
        index = SHORT1FROMMR(WinSendMsg(launcher.listHistory, LM_INSERTITEM, MPFROMSHORT(0), MPFROMP(p)));
        if (index != LIT_NONE)
          WinSendMsg(launcher.listHistory, LM_SETITEMHANDLE, MPFROMSHORT(index), MPFROMP(waddata));
        
        free(p);
        p = NULL;
      }

      free(str);
    }
  }
}

/*
BOOL CALLBACK LauncherClientCallback (HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
  case WM_INITDIALOG:
    {
      int i;
      HMODULE hMod;
      waddata_t data;

      launcher.HWNDClient = hDlg;
      launcher.listIWAD = GetDlgItem(launcher.HWNDClient, IDC_IWADCOMBO);
      launcher.listPWAD = GetDlgItem(launcher.HWNDClient, IDC_PWADLIST);
      launcher.listHistory = GetDlgItem(launcher.HWNDClient, IDC_HISTORYCOMBO);
      launcher.listCMD = GetDlgItem(launcher.HWNDClient, IDC_COMMANDCOMBO);
      launcher.staticFileName = GetDlgItem(launcher.HWNDClient, IDC_FULLFILENAMESTATIC);

      WinSendMsg(launcher.listCMD, CB_ADDSTRING, 0, (LPARAM)"Rebuild the "PACKAGE_NAME" cache");
      WinSendMsg(launcher.listCMD, CB_ADDSTRING, 0, (LPARAM)"Clear all Launcher's history");
      WinSendMsg(launcher.listCMD, CB_ADDSTRING, 0, (LPARAM)"Associate the current EXE with DOOM wads");
      WinSendMsg(launcher.listCMD, CB_ADDSTRING, 0, (LPARAM)"... with DOOM demos");
      WinSendMsg(launcher.listCMD, CB_ADDSTRING, 0, (LPARAM)"... with DOOM demos (-auto mode)");

      {
        char buf[128];
        strcpy(buf, ((launcher_enable + 1) % launcher_enable_count == launcher_enable_never ? "Disable" : "Enable"));
        strcat(buf, " this Launcher for future use");
        WinSendMsg(launcher.listCMD, CB_ADDSTRING, 0, (LPARAM)buf);
      }

      WinSendMsg(launcher.listCMD, CB_SETCURSEL, -1, 0);
      L_CommandOnChange();

      L_ReadCacheData();
      
      e6y_I_FindFile("*.wad");
      e6y_I_FindFile("*.deh");
      e6y_I_FindFile("*.bex");

      L_FillGameList();
      L_FillHistoryList();

      i = -1;
      if (launcher_params)
      {
        WadDataInit(&data);
        WadFilesToWadData(&data);
        L_GUISelect(&data);
      }
      else
      {
#ifdef HAVE_LIBPCREPOSIX
        for (i = 0; (size_t)i < numwadfiles; i++)
        {
          if (wadfiles[i].src == source_lmp)
          {
            patterndata_t patterndata;
            memset(&patterndata, 0, sizeof(patterndata));

            if (DemoNameToWadData(wadfiles[i].name, &data, &patterndata))
            {
              L_GUISelect(&data);
              WinSendMsg(launcher.staticFileName, WM_SETTEXT, 0, (LPARAM)patterndata.pattern_name);
              WadDataFree(&data);
              break;
            }
            free(patterndata.missed);
          }
        }
#endif
      }
      
      if ((size_t)i == numwadfiles)
      {
        if (WinSendMsg(launcher.listHistory, CB_SETCURSEL, 0, 0) != CB_ERR)
        {
          L_HistoryOnChange();
          SetFocus(launcher.listHistory);
        }
        else if (WinSendMsg(launcher.listIWAD, CB_SETCURSEL, 0, 0) != CB_ERR)
        {
          L_GameOnChange();
          SetFocus(launcher.listPWAD);
        }
      }
    }
		break;

  case WM_NOTIFY:
    OnWMNotify(lParam);
    break;

  case WM_COMMAND:
    {
      int wmId    = LOWORD(wParam);
      int wmEvent = HIWORD(wParam);

      if (wmId == IDC_PWADLIST && wmEvent == LBN_DBLCLK)
      {
        if (L_PrepareToLaunch())
          EndDialog (launcher.HWNDLauncher, 1);
      }
      
      if (wmId == IDC_HISTORYCOMBO && wmEvent == CBN_SELCHANGE)
        L_HistoryOnChange();
      
      if (wmId == IDC_IWADCOMBO && wmEvent == CBN_SELCHANGE)
        L_GameOnChange();
      
      if (wmId == IDC_PWADLIST && wmEvent == LBN_SELCHANGE)
        L_FilesOnChange();

      if ((wmId == IDC_IWADCOMBO && wmEvent == CBN_SELCHANGE) ||
        (wmId == IDC_PWADLIST && wmEvent == LBN_SELCHANGE))
        WindSendDlgItemMsg(launcher.listHistory, CB_SETCURSEL, -1, 0);

      if (wmId == IDC_COMMANDCOMBO && wmEvent == CBN_SELCHANGE)
        L_CommandOnChange();
    }
    break;
	}
	return FALSE;
  }
*/

MRESULT EXPENTRY LauncherCallback (HWND hWnd, ULONG message, MPARAM wParam, MPARAM lParam)
{
  int wmId, wmEvent;
  
  switch (message)
  {
  case WM_COMMAND:
    wmId    = SHORT1FROMMP(wParam);
    wmEvent = SHORT2FROMMP(wParam);

    switch (wmId)
    {
    case DID_CANCEL:
      WinDismissDlg(hWnd, 0);
      break;

    case DID_OK:
      if (L_PrepareToLaunch())
      {
        WinDismissDlg(hWnd, 1);
      }
      break;
      
    case IDC_COMMANDCOMBO: 
      if (wmEvent == LN_SELECT)
      {
        L_CommandOnChange();
      }
    }
    break;
    
  case WM_INITDLG:
      {
        int i;
        HMODULE hMod;
        waddata_t data;

        launcher.HWNDLauncher = hWnd;
        launcher.listIWAD = WinWindowFromID(launcher.HWNDLauncher, IDC_IWADCOMBO);
        launcher.listPWAD = WinWindowFromID(launcher.HWNDLauncher, IDC_PWADLIST);
        launcher.listHistory = WinWindowFromID(launcher.HWNDLauncher, IDC_HISTORYCOMBO);
        launcher.listCMD = WinWindowFromID(launcher.HWNDLauncher, IDC_COMMANDCOMBO);
        launcher.staticFileName = WinWindowFromID(launcher.HWNDLauncher, IDC_FULLFILENAMESTATIC);
printf("%p %p %p %p %p %p\n", launcher.HWNDLauncher, launcher.listIWAD, launcher.listPWAD, launcher.listHistory, launcher.listCMD, launcher.staticFileName);
        
        // Fill the commands combobox
        WinSendMsg(launcher.listCMD, LM_INSERTITEM, MPFROMSHORT(LIT_END), MPFROMP("Rebuild the "PACKAGE_NAME" cache"));
        WinSendMsg(launcher.listCMD, LM_INSERTITEM, MPFROMSHORT(LIT_END), MPFROMP("Clear all Launcher's history"));
        WinSendMsg(launcher.listCMD, LM_INSERTITEM, MPFROMSHORT(LIT_END), MPFROMP("Associate the current EXE with DOOM wads"));
        WinSendMsg(launcher.listCMD, LM_INSERTITEM, MPFROMSHORT(LIT_END), MPFROMP("... with DOOM demos"));
        WinSendMsg(launcher.listCMD, LM_INSERTITEM, MPFROMSHORT(LIT_END), MPFROMP("... with DOOM demos (-auto mode)"));
  
        {
          char buf[128];
          strcpy(buf, ((launcher_enable + 1) % launcher_enable_count == launcher_enable_never ? "Disable" : "Enable"));
          strcat(buf, " this Launcher for future use");
          WinSendMsg(launcher.listCMD, LM_INSERTITEM, MPFROMSHORT(LIT_END), MPFROMP(buf));
        }
  
        WinSendMsg(launcher.listCMD, LM_SELECTITEM, MPFROMSHORT(LIT_NONE), MPFROMSHORT(0));
        L_CommandOnChange();
        
        L_ReadCacheData();
        
        e6y_I_FindFile("*.wad");
        e6y_I_FindFile("*.deh");
        e6y_I_FindFile("*.bex");
  
        L_FillGameList();
/*        L_FillHistoryList();

        i = -1;
        if (launcher_params)
        {
          WadDataInit(&data);
          WadFilesToWadData(&data);
          L_GUISelect(&data);
        }
        else
        {
  #ifdef HAVE_LIBPCREPOSIX
          for (i = 0; (size_t)i < numwadfiles; i++)
          {
            if (wadfiles[i].src == source_lmp)
            {
              patterndata_t patterndata;
              memset(&patterndata, 0, sizeof(patterndata));
  
              if (DemoNameToWadData(wadfiles[i].name, &data, &patterndata))
              {
                L_GUISelect(&data);
                // TODO WinSendMsg(launcher.staticFileName, WM_SETTEXT, 0, (LPARAM)patterndata.pattern_name);
                WadDataFree(&data);
                break;
              }
              free(patterndata.missed);
            }
          }
  #endif
        }
        
        if ((size_t)i == numwadfiles)
        {
          if (SHORT1FROMMR(WinSendMsg(launcher.listHistory, LM_QUERYSELECTION, MPFROMSHORT(0), MPFROMSHORT(0))) != LIT_NONE)
          {
            L_HistoryOnChange();
            WinSetFocus(HWND_DESKTOP, launcher.listHistory);
          }
          else if(WinSendMsg(launcher.listIWAD, LM_SELECTITEM, MPFROMSHORT(0), MPFROMSHORT(0)) != 0)
          {
            L_GameOnChange();
            WinSetFocus(HWND_DESKTOP, launcher.listPWAD);
          }
        } */
      }
      
      WinUpdateWindow(launcher.HWNDLauncher);
      return 1;
      
  case WM_DESTROY:
    L_HistoryFreeData();
    break;
  default:
      /*
       * Any event messages that the dialog procedure has not processed
       * come here and are processed by WinDefDlgProc.
       * This call MUST exist in your dialog procedure.
       */
      return WinDefDlgProc(hWnd, message, wParam, lParam);
  }
  return 0;
}

static ULONG L_LauncherIsNeeded(void)
{
  int i;
  dboolean pwad = false;
  char *iwad = NULL;

//  SHIFT for invert
//  if (GetAsyncKeyState(VK_SHIFT) ? launcher_enable : !launcher_enable)
//    return false;

  if ((WinGetKeyState(HWND_DESKTOP, VK_SHIFT) & 0x8000))
    return true;

  if (launcher_enable == launcher_enable_always)
    return true;

  if (launcher_enable == launcher_enable_never)
    return false;

  i = M_CheckParm("-iwad");
  if (i && (++i < myargc))
    iwad = I_FindFile(myargv[i], ".wad");

  for (i=0; !pwad && i < (int)numwadfiles; i++)
    pwad = wadfiles[i].src == source_pwad;

  return (!iwad && !pwad && !M_CheckParm("-auto"));
}

void LauncherShow(unsigned int params)
{
  QMSG  qmsg;
  BOOL success;


  if (!L_LauncherIsNeeded())
  {
    return;
  }

  launcher_params = params;

  sprintf(launchercachefile,"%s/"PACKAGE_TARNAME".cache", I_DoomExeDir());

  success = WinDlgBox(HWND_DESKTOP,    /* Place anywhere on desktop    */
                      HWND_DESKTOP,    /* Owned by desk top            */
                      (PFNWP)LauncherCallback,   /* Addr. of procedure  */
                      (HMODULE)0,      /* Module handle                */
                      IDD_LAUNCHERSERVERDIALOG,     /* Dialog identifier in resource*/
                      NULL);           /* Initialization data          */
  
  printf("Dialog success %ld %ld\n",success, WinGetLastError(0));
  if (!success)
  {
    printf("WinDlgBox\n");
    I_SafeExit(0);
  }

  I_SafeExit(0);
  switch (success)
  {
  case 0:
    I_SafeExit(-1);
    break;
  case 1:
    M_SaveDefaults();
    break;
  }
}

#endif // OS2
