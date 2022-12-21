/* Name: main.cpp
 * Project: MrSwitcher
 * Author Francis Gradel, B.Eng. (Retronic Design)
 * Modified Date: 2022-01-01
 * Tabsize: 4
 * License: Proprietary, free under certain conditions. See Documentation.
 */
//#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <windows.h>
#include <commctrl.h>
#include "resource.h"
#include "usbcalls.h"

#define IDENT_VENDOR_NUM        0x16c0
#define IDENT_VENDOR_STRING     "obdev.at"
#define IDENT_PRODUCT_NUM       0x05df
#define IDENT_PRODUCT_STRING    "HIDBoot"

#define IDENT_VENDOR_NUM_JOY       0x16c0
#define IDENT_PRODUCT_NUM_JOY      0x27dc
#define IDENT_VENDOR_STRING_JOY    "retronicdesign.com"

#define IDENT_VENDOR_NUM_JOY2       0x0810
#define IDENT_PRODUCT_NUM_JOY2      0xe501
#define IDENT_VENDOR_STRING_JOY2    "retronicdesign.com"

#define IDENT_VENDOR_NUM_MOUSE       0x16c0
#define IDENT_PRODUCT_NUM_MOUSE      0x27da
#define IDENT_VENDOR_STRING_MOUSE    "retronicdesign.com"

OPENFILENAME ofn;       // common dialog box structure
TCHAR szFile[260] = { 0 };       // if using TCHAR macros

int CustomMessageBox(HWND hWnd,
                     LPCTSTR lpText,
                     LPCTSTR lpCaption,
                     UINT uType,
                     UINT uIconResID)
{
   MSGBOXPARAMS mbp;
   mbp.cbSize = sizeof(MSGBOXPARAMS);
   mbp.hwndOwner = hWnd;
   mbp.hInstance = GetModuleHandle(NULL);
   mbp.lpszText = lpText;
   mbp.lpszCaption = lpCaption;
   mbp.dwStyle = uType | MB_USERICON;
   mbp.dwLanguageId = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
   mbp.lpfnMsgBoxCallback = NULL;
   mbp.dwContextHelpId = 0;
   mbp.lpszIcon = MAKEINTRESOURCE(uIconResID);

   return MessageBoxIndirect(&mbp);
}
char _filename[250];
char _vendor[100];
char _device[100];
char _vid[100];
int vid;
char _pid[100];
int pid;

static char dataBuffer[65536 + 256];    /* buffer for file data */
static int  startAddress, endAddress;

typedef struct HIDSetReport_t{
    char    reportId;
    char    data;
}HIDSetReport_t;

HIDSetReport_t buf;

static int  parseUntilColon(FILE *fp)
{
int c;

    do{
        c = getc(fp);
    }while(c != ':' && c != EOF);
    return c;
}

static int  parseHex(FILE *fp, int numDigits)
{
int     i;
char    temp[9];

    for(i = 0; i < numDigits; i++)
        temp[i] = getc(fp);
    temp[i] = 0;
    return strtol(temp, NULL, 16);
}

/* ------------------------------------------------------------------------- */

static int  parseIntelHex(HWND hwndDlg, char *hexfile, char buffer[65536 + 256], int *startAddr, int *endAddr)
{
int     address, base, d, segment, i, lineLen, sum;
FILE    *input;
char str[512];

    input = fopen(hexfile, "r");
    if(input == NULL){
        sprintf(str, "error opening %s: %s", hexfile, strerror(errno));
		MessageBox(hwndDlg, str, "Error", MB_ICONEXCLAMATION);
        return 1;
    }
    while(parseUntilColon(input) == ':'){
        sum = 0;
        sum += lineLen = parseHex(input, 2);
        base = address = parseHex(input, 4);
        sum += address >> 8;
        sum += address;
        sum += segment = parseHex(input, 2);  /* segment value? */
        if(segment != 0)    /* ignore lines where this byte is not 0 */
            continue;
        for(i = 0; i < lineLen ; i++){
            d = parseHex(input, 2);
            buffer[address++] = d;
            sum += d;
        }
        sum += parseHex(input, 2);
        if((sum & 0xff) != 0){
            sprintf(str, "Checksum error between address 0x%x and 0x%x", base, address);
			MessageBox(hwndDlg, str, "Warning", MB_ICONEXCLAMATION);
        }
        if(*startAddr > base)
            *startAddr = base;
        if(*endAddr < address)
            *endAddr = address;
    }
    fclose(input);
    return 0;
}

char    *usbErrorMessage(int errCode)
{
static char buffer[80];

    switch(errCode){
        case USB_ERROR_ACCESS:      return "Access to device denied";
        case USB_ERROR_NOTFOUND:    return "The specified device was not found";
        case USB_ERROR_BUSY:        return "The device is used by another application";
        case USB_ERROR_IO:          return "Communication error with device";
        default:
            sprintf(buffer, "Unknown USB error %d", errCode);
            return buffer;
    }
    return NULL;    /* not reached */
}

static int  getUsbInt(char *buffer, int numBytes)
{
int shift = 0, value = 0, i;

    for(i = 0; i < numBytes; i++){
        value |= ((int)*buffer & 0xff) << shift;
        shift += 8;
        buffer++;
    }
    return value;
}

static void setUsbInt(char *buffer, int value, int numBytes)
{
int i;

    for(i = 0; i < numBytes; i++){
        *buffer++ = value;
        value >>= 8;
    }
}

/* ------------------------------------------------------------------------- */

typedef struct deviceInfo{
    char    reportId;
    char    pageSize[2];
    char    flashSize[4];
}deviceInfo_t;

typedef struct deviceData{
    char    reportId;
    char    address[3];
    char    data[128];
}deviceData_t;

static int uploadData(HWND hwndDlg, char *dataBuffer, int startAddr, int endAddr)
{
usbDevice_t *dev = NULL;
int         err = 0, len, mask, pageSize, deviceSize;
union{
    char            bytes[1];
    deviceInfo_t    info;
    deviceData_t    data;
}           buffer;
char str[512];

    if((err = usbOpenDevice(&dev, IDENT_VENDOR_NUM, IDENT_VENDOR_STRING, IDENT_PRODUCT_NUM, IDENT_PRODUCT_STRING, 1)) != 0){
        fprintf(stderr, "Error opening HIDBoot device: %s\n", usbErrorMessage(err));
        goto errorOccurred;
    }
    len = sizeof(buffer);
    if(endAddr > startAddr){    // we need to upload data
        if((err = usbGetReport(dev, USB_HID_REPORT_TYPE_FEATURE, 1, buffer.bytes, &len)) != 0){
            sprintf(str, "Error reading page size: %s\n", usbErrorMessage(err));
			MessageBox(hwndDlg, str, "Error", MB_ICONEXCLAMATION);
            goto errorOccurred;
        }
        if(len < sizeof(buffer.info)){
            sprintf(str, "Not enough bytes in device info report (%d instead of %d)\n", len, (int)sizeof(buffer.info));
			MessageBox(hwndDlg, str, "Error", MB_ICONEXCLAMATION);
            err = -1;
            goto errorOccurred;
        }
        pageSize = getUsbInt(buffer.info.pageSize, 2);
        deviceSize = getUsbInt(buffer.info.flashSize, 4);
        //sprintf(str,"Page size   = %d (0x%x)\nDevice size = %d (0x%x); %d bytes remaining\n", pageSize, pageSize, deviceSize, deviceSize, deviceSize - 2048);
		//MessageBox(hwndDlg, str, "Information", MB_ICONINFORMATION);
        if(endAddr > deviceSize - 2048){
            sprintf(str, "Data (%d bytes) exceeds remaining flash size!\n", endAddr);
			MessageBox(hwndDlg, str, "Error", MB_ICONEXCLAMATION);
            err = -1;
            goto errorOccurred;
        }
        if(pageSize < 128){
            mask = 127;
        }else{
            mask = pageSize - 1;
        }
        startAddr &= ~mask;                  /* round down */
        endAddr = (endAddr + mask) & ~mask;  /* round up */
        //sprintf(str,"Uploading %d (0x%x) bytes starting at %d (0x%x)\n", endAddr - startAddr, endAddr - startAddr, startAddr, startAddr);
		//MessageBox(hwndDlg, str, "Information", MB_ICONINFORMATION);
		SendMessage(GetDlgItem(hwndDlg, PROGRESSION), PBM_SETRANGE, 0, MAKELPARAM(startAddr, endAddr));
		SendMessage(GetDlgItem(hwndDlg, PROGRESSION), PBM_SETSTEP, (WPARAM) sizeof(buffer.data.data), 0); 
		
        while(startAddr < endAddr){
            buffer.data.reportId = 2;
            memcpy(buffer.data.data, dataBuffer + startAddr, 128);
            setUsbInt(buffer.data.address, startAddr, 3);
			SendMessage(GetDlgItem(hwndDlg, PROGRESSION), PBM_STEPIT, 0, 0); 
            //printf("\r0x%05x ... 0x%05x", startAddr, startAddr + (int)sizeof(buffer.data.data));
            //fflush(stdout);
            if((err = usbSetReport(dev, USB_HID_REPORT_TYPE_FEATURE, buffer.bytes, sizeof(buffer.data))) != 0){
                sprintf(str, "Error uploading data block: %s\n", usbErrorMessage(err));
				MessageBox(hwndDlg, str, "Error", MB_ICONEXCLAMATION);
                goto errorOccurred;
            }
            startAddr += sizeof(buffer.data.data);
        }
        printf("\n");
    }
    /* and now leave boot loader: */
    buffer.info.reportId = 1;
    usbSetReport(dev, USB_HID_REPORT_TYPE_FEATURE, buffer.bytes, sizeof(buffer.info));
    /* Ignore errors here. If the device reboots before we poll the response,
     * this request fails.
     */

errorOccurred:
    if(dev != NULL)
        usbCloseDevice(dev);
    return err;
}

int ScanUSB(HWND hwndDlg)
{
	//Vider les listes
	SendMessage(GetDlgItem(hwndDlg,USBVENDOR), LB_RESETCONTENT, 0, 0);
    SendMessage(GetDlgItem(hwndDlg,USBDEVICE), LB_RESETCONTENT, 0, 0);
	SendMessage(GetDlgItem(hwndDlg,USBVID), LB_RESETCONTENT, 0, 0);
    SendMessage(GetDlgItem(hwndDlg,USBPID), LB_RESETCONTENT, 0, 0);
	int nbdevice=0;
	nbdevice = usbListDevice(GetDlgItem(hwndDlg,USBVENDOR),GetDlgItem(hwndDlg,USBDEVICE),GetDlgItem(hwndDlg,USBVID),GetDlgItem(hwndDlg,USBPID),IDENT_VENDOR_NUM,IDENT_PRODUCT_NUM);
	nbdevice += usbListDevice(GetDlgItem(hwndDlg,USBVENDOR),GetDlgItem(hwndDlg,USBDEVICE),GetDlgItem(hwndDlg,USBVID),GetDlgItem(hwndDlg,USBPID),IDENT_VENDOR_NUM_JOY,IDENT_PRODUCT_NUM_JOY);
	nbdevice += usbListDevice(GetDlgItem(hwndDlg,USBVENDOR),GetDlgItem(hwndDlg,USBDEVICE),GetDlgItem(hwndDlg,USBVID),GetDlgItem(hwndDlg,USBPID),IDENT_VENDOR_NUM_JOY2,IDENT_PRODUCT_NUM_JOY2);
	nbdevice += usbListDevice(GetDlgItem(hwndDlg,USBVENDOR),GetDlgItem(hwndDlg,USBDEVICE),GetDlgItem(hwndDlg,USBVID),GetDlgItem(hwndDlg,USBPID),IDENT_VENDOR_NUM_MOUSE,IDENT_PRODUCT_NUM_MOUSE);
	//Sélectionner le premier device détecté
	if(nbdevice!=0)
		SendMessage(GetDlgItem(hwndDlg,USBDEVICE), LB_SETCURSEL, 0, 1);
	return TRUE;
}

BOOL CALLBACK DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{

    switch(uMsg)
    {
		// S'exécute à l'initiation du dialogue
		case WM_INITDIALOG:
			//Change l'icône de la barre de titre
			SendMessage(hwndDlg, WM_SETICON, ICON_SMALL,(LPARAM)LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_MAINICON)));
			//Scan USB devices and populate list object
			ScanUSB(hwndDlg);
            return TRUE;
		// Bouton CANCLOSE	
        case WM_CLOSE:
            EndDialog(hwndDlg, 0);
            return TRUE;
		// Action de l'utilisateur détectée...
        case WM_COMMAND:
            switch(LOWORD(wParam))
            {
				//Bouton CANCEL
                case IDCANCEL:
                    EndDialog(hwndDlg, 0);
                    return TRUE;
				// Bouton SELECT/OPEN
                case IDOPEN:
                    // Ouverture du dialogue de sélection de fichier HEX
					ZeroMemory(&ofn, sizeof(ofn));
					ofn.lStructSize = sizeof(ofn);
					ofn.hwndOwner = hwndDlg;
					ofn.lpstrFile = szFile;
					ofn.nMaxFile = sizeof(szFile);
					ofn.lpstrFilter = "Intel HEX files\0*.hex\0";
					ofn.nFilterIndex = 1;
					ofn.lpstrFileTitle = NULL;
					ofn.nMaxFileTitle = 0;
					ofn.lpstrInitialDir = NULL;
					ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
					if (GetOpenFileName(&ofn) == TRUE)
					{
						//Assignation du nom de fichier et chemin à la case de texte.
						SetDlgItemText( hwndDlg, FILENAME, ofn.lpstrFile );
					}
                    return TRUE;
				// Bouton ReScan
                case IDRESCAN:
					ScanUSB(hwndDlg);
                    return TRUE;
				// Bouton SWITCH
                case IDSWITCH:
					//Nom du fichier à programmer
					GetWindowText(GetDlgItem(hwndDlg, FILENAME), _filename, sizeof(_filename));
					//Information du device à programmer
					int itemIndex = (int) SendMessage(GetDlgItem(hwndDlg, USBDEVICE), LB_GETCURSEL, (WPARAM)0, (LPARAM) 0);
					if (itemIndex != LB_ERR)
					{
						SendMessage(GetDlgItem(hwndDlg, USBVENDOR), LB_GETTEXT, (WPARAM) itemIndex, (LPARAM) _vendor );
						SendMessage(GetDlgItem(hwndDlg, USBDEVICE), LB_GETTEXT, (WPARAM) itemIndex, (LPARAM) _device );
						SendMessage(GetDlgItem(hwndDlg, USBVID), LB_GETTEXT, (WPARAM) itemIndex, (LPARAM) _vid );
						SendMessage(GetDlgItem(hwndDlg, USBPID), LB_GETTEXT, (WPARAM) itemIndex, (LPARAM) _pid );
					}
					//Si nous n'avons pas de device sélectionné, erreur!
					if(_device[0]==0)
					{
						MessageBox(hwndDlg, "No connected device selected!", "Error", MB_ICONEXCLAMATION);
						return TRUE;
					}
					//Si nous n'avons pas de fichier sélectionné, erreur!
					if(_filename[0]==0)
					{
						MessageBox(hwndDlg, "No new functionality selected!", "Error", MB_ICONEXCLAMATION);
						return TRUE;
					}
					//Conversion en integer du VID et PID
					sscanf(_vid,"%x",&vid);
					sscanf(_pid,"%x",&pid);

					//Si device et fichier ok, on procède...
					SendMessage(GetDlgItem(hwndDlg, PROGRESSION), PBM_SETPOS, (WPARAM)0, 0);
					EnableWindow(GetDlgItem(hwndDlg, IDSWITCH), FALSE);
					EnableWindow(GetDlgItem(hwndDlg, IDRESCAN), FALSE);
					EnableWindow(GetDlgItem(hwndDlg, IDHELP), FALSE);
					EnableWindow(GetDlgItem(hwndDlg, IDOPEN), FALSE);
					EnableWindow(GetDlgItem(hwndDlg, USBDEVICE), FALSE);
					EnableWindow(GetDlgItem(hwndDlg, FILENAME), FALSE);
					usbDevice_t *dev = NULL;
					ShowWindow(GetDlgItem(hwndDlg, TXTPROGRESSION), SW_SHOW);
					//Si c'est un device non HIDboot, le mettre en mode HIDboot.
					if(strcmp(_vendor,IDENT_VENDOR_STRING_JOY)==0)
					{
						//MessageBox(hwndDlg, "Requesting HIDboot", "Error", MB_ICONEXCLAMATION);
						SetDlgItemText(hwndDlg, TXTPROGRESSION, "Requesting HIDboot...");
						//Attente du reboot du device et test en boucle pour HIDboot.
						usbOpenDevice(&dev, vid, _vendor, pid, _device, 1);
						buf.reportId = 0;
						buf.data = 0x5A;
						int i=0;
						usbSetReport(dev, USB_HID_REPORT_TYPE_FEATURE, (char *)&buf, sizeof(buf));
						while(usbOpenDevice(&dev, IDENT_VENDOR_NUM, IDENT_VENDOR_STRING, IDENT_PRODUCT_NUM, IDENT_PRODUCT_STRING , 1)&&i<40)
						{
							i++;
							Sleep(100);
						}
						//Fermer le device temporaire de détection.
						if(dev != NULL)
							usbCloseDevice(dev);
						//Si nous avons atteint le timeout, message d'erreur et fin de la manoeuvre.
						if(i>=40)
						{
							MessageBox(hwndDlg, "Adapter did not respond to HIDboot request.\nPlease invoke manual request by holding button 1 while connecting USB adapter.", "Error", MB_ICONEXCLAMATION);
							//Scan USB devices and populate list object
							ScanUSB(hwndDlg);
							// Re-enable buttons
							EnableWindow(GetDlgItem(hwndDlg, IDSWITCH), TRUE);
							EnableWindow(GetDlgItem(hwndDlg, IDRESCAN), TRUE);
							EnableWindow(GetDlgItem(hwndDlg, IDHELP), TRUE);
							EnableWindow(GetDlgItem(hwndDlg, IDOPEN), TRUE);
							EnableWindow(GetDlgItem(hwndDlg, USBDEVICE), TRUE);
							EnableWindow(GetDlgItem(hwndDlg, FILENAME), TRUE);
							SetDlgItemText(hwndDlg, TXTPROGRESSION, "Failed!");
							return TRUE;
						}						
					}
					//Programmer le HIDboot
					SetDlgItemText(hwndDlg, TXTPROGRESSION, "Switching...");
					
					startAddress = sizeof(dataBuffer);
					endAddress = 0;
					memset(dataBuffer, -1, sizeof(dataBuffer));
					//Chargement du fichier Intel HEX
					parseIntelHex(hwndDlg,_filename, dataBuffer, &startAddress, &endAddress);
					//Programmation du flash
					uploadData(hwndDlg,dataBuffer, startAddress, endAddress);
					
					//Scan USB devices and populate list object
					Sleep(1000);
					ScanUSB(hwndDlg);
					// Re-enable buttons
					EnableWindow(GetDlgItem(hwndDlg, IDSWITCH), TRUE);
					EnableWindow(GetDlgItem(hwndDlg, IDRESCAN), TRUE);
					EnableWindow(GetDlgItem(hwndDlg, IDHELP), TRUE);
					EnableWindow(GetDlgItem(hwndDlg, IDOPEN), TRUE);
					EnableWindow(GetDlgItem(hwndDlg, USBDEVICE), TRUE);
					EnableWindow(GetDlgItem(hwndDlg, FILENAME), TRUE);
					
					SetDlgItemText(hwndDlg, TXTPROGRESSION, "Done!");

                    return TRUE;
				// Bouton HELP	/ ABOUT
				case IDHELP:
					CustomMessageBox(hwndDlg, "(C) 2022 Retronic Design\nVersion 1.0 (2022-01-01)\n\n**Note:**\nIf your controller is not listed or not responding, disconnect the USB adapter and reconnect it while holding button 1.\nIt will then be shown as \"HIDboot\".","About Mr.Switcher", MB_OK, IDI_MAINICON);
					return TRUE;
            }
    }

    return FALSE;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	return DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, (DLGPROC)DialogProc);
}