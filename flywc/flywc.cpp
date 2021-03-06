// flywc.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "flywc.h"

#define MAX_LOADSTRING 100
#define MAX_TITLE_LENGTH 512
#define WATCHDOG_INTERVAL 500
#define WND_WIDTH 250
#define WND_HEIGHT 150
#define CFG_FILE "flywc.cfg"

#define NEUZ_PROCESS L"Neuz.exe"
#define NEUZ_WINDOW L"FLYFF"
#define NEUZ_CLASS L"saida"

#define SEARCH_MODE_PROCESS 1
#define SEARCH_MODE_WND_TITLE 2
#define SEARCH_MODE_WND_CLASS 3

#define _TRY(x) { int nt = 0; while(!x) { if(nt++ > 10) { nError = -1; break; } } } // 10 retries for a command

// Global Variables:
HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];

// Structures:
struct wndinfo
{
    HWND hWnd;
    DWORD dwPid;
    wchar_t *title;
    long lStyle;
    RECT rcSize;
};

struct wndhelp
{
    struct wndinfo wi;
    TCITEM tab;
};

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    TabProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    AboutProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    SettingsProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    ChangeTextProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    WndAddProc(HWND, UINT, WPARAM, LPARAM);
void                OnCommand(HWND, int, HWND, UINT);
LRESULT             OnNotify(HWND, int, LPNMHDR);
void                OnDestroy(HWND);

int writeConfig(bool, bool, int, wchar_t *);
int readConfig();
void removeAllWindows();
void restoreWindow(struct wndinfo *);
void selectFirstTab();
void WatchdogThread();
void DoAddWindow(bool = false, bool = false);
void DoDeleteWindow(int = -1);
BOOL CALLBACK EnumWindowsProc(HWND, LPARAM);

// Variables:
int nScrWidth;
int nScrHeight;
HWND hwndParent;
HWND hwndTab;
RECT rcParentClient;
bool bAutoAdd, bMinimize;
bool bQuit;
wchar_t cFindTargetName[MAX_PATH];
WNDPROC prevWndProc;
int nSelIndexText;
int nSearchMode;

std::vector<struct wndhelp> arTabs;
std::vector<struct wndinfo> arWnds;

std::thread thWatchdog;
std::mutex mtxTab;

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    HACCEL hAccelTable;
    MSG msg;

    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    nScrWidth = GetSystemMetrics(SM_CXSCREEN);
    nScrHeight = GetSystemMetrics(SM_CYSCREEN);

    hwndParent = NULL;
    hwndTab = NULL;
    bAutoAdd = false;
    bMinimize = true;
    bQuit = false;
    wsprintf(cFindTargetName, NEUZ_PROCESS);
    nSelIndexText = -1;
    nSearchMode = SEARCH_MODE_PROCESS;

    arTabs.clear();
    arWnds.clear();

    readConfig();

    // Initialize global strings
    LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadString(hInstance, IDC_FLYWC, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if(!InitInstance(hInstance, nCmdShow))
    {
        return FALSE;
    }

    hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_FLYWC));

    if(bAutoAdd)
    {
        DoAddWindow(true, true); // automatisch alle Fenster hinzufügen
    }

    thWatchdog = std::thread(WatchdogThread);
    thWatchdog.detach();

    // Main message loop:
    while(GetMessage(&msg, NULL, 0, 0))
    {
        if(!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int)msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_FLYWC));
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCE(IDC_FLYWC);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassEx(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance; // Store instance handle in our global variable

    hwndParent = CreateWindow(szWindowClass, szTitle, 
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPSIBLINGS /*| WS_MAXIMIZEBOX*/,
        CW_USEDEFAULT, CW_USEDEFAULT, WND_WIDTH, WND_HEIGHT, NULL, NULL, hInstance, NULL);

    if(!hwndParent)
    {
        return FALSE;
    }

    GetClientRect(hwndParent, &rcParentClient);

    hwndTab = CreateWindow(WC_TABCONTROL, L"", 
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, rcParentClient.right, rcParentClient.bottom, hwndParent, NULL, hInstance, NULL);

    prevWndProc = (WNDPROC)SetWindowLong(hwndTab, AC_SRC_ALPHA, (LONG_PTR)&TabProc);

    ShowWindow(hwndParent, nCmdShow);
    UpdateWindow(hwndParent);

    InitCommonControls();

    return TRUE;
}

LRESULT CALLBACK TabProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int nSelIndex = 0;
    TCHITTESTINFO pinfo;

    switch(message)
    {
    case WM_MBUTTONUP:
    case WM_LBUTTONDBLCLK:
        pinfo.pt.x = GET_X_LPARAM(lParam);
        pinfo.pt.y = GET_Y_LPARAM(lParam);
        pinfo.flags = (TCHT_ONITEM | TCHT_ONITEMICON | TCHT_ONITEMLABEL);

        nSelIndex = TabCtrl_HitTest(hwndTab, &pinfo);

        if(nSelIndex >= 0)
        {
            nSelIndexText = nSelIndex;

            if(message == WM_LBUTTONDBLCLK)
                DialogBox(hInst, MAKEINTRESOURCE(IDD_CHANGE_TEXT), hwndParent, ChangeTextProc);
            else
                DoDeleteWindow(nSelIndex);
        }

        break;
    }

    return CallWindowProc(prevWndProc, hWnd, message, wParam, lParam);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int nSelIndex = 0;

    switch(message)
    {
    case WM_ACTIVATE:
        
        break;

    case WM_SYSCOMMAND:
        switch(wParam)
        {
        case SC_MINIMIZE:
        case SC_RESTORE:
            nSelIndex = TabCtrl_GetCurSel(hwndTab);

            if(nSelIndex >= 0 && (bMinimize || wParam == SC_RESTORE))
            {
                ShowWindow(arTabs[nSelIndex].wi.hWnd, (wParam == SC_MINIMIZE ? SW_FORCEMINIMIZE : SW_RESTORE));
            }

            break;
        }
        break;

        HANDLE_MSG(hWnd, WM_COMMAND, OnCommand);
        HANDLE_MSG(hWnd, WM_NOTIFY, OnNotify);
        HANDLE_MSG(hWnd, WM_DESTROY, OnDestroy);
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

INT_PTR CALLBACK AboutProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch(message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if(LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }

    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK SettingsProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND hwndTmp = NULL;
    wchar_t cTmp[MAX_PATH];
    UINT nValue = 0;
    int nControlId = 0;
    bool bCheckValue = false;
    bool *pSelValue = NULL;

    UNREFERENCED_PARAMETER(lParam);

    switch(message)
    {
    case WM_INITDIALOG:
        hwndTmp = GetDlgItem(hDlg, IDC_EDIT_PROCESS);

        if(hwndTmp != NULL)
        {
            SetWindowText(hwndTmp, cFindTargetName);
        }

        for(int i = 0; i < 3; i++) // check radio buttons or checkboxes
        {
            switch(i)
            {
            case 0:
                nControlId = IDC_CHECK_STARTUP;
                bCheckValue = bAutoAdd;
                break;

            case 1:
                nControlId = IDC_CHECK_MINIMIZE;
                bCheckValue = bMinimize;
                break;

            case 2:
                bCheckValue = true; // others unchecked automatically

                switch(nSearchMode)
                {
                case SEARCH_MODE_PROCESS:
                    nControlId = IDC_RADIO_SEARCH_PROCESS;
                    break;

                case SEARCH_MODE_WND_TITLE:
                    nControlId = IDC_RADIO_SEARCH_TITLE;
                    break;

                case SEARCH_MODE_WND_CLASS:
                    nControlId = IDC_RADIO_SEARCH_CLASS;
                    break;
                }

                break;
            }

            hwndTmp = GetDlgItem(hDlg, nControlId);

            if(hwndTmp != NULL)
            {
                SendMessage(hwndTmp, BM_SETCHECK, bCheckValue ? BST_CHECKED : BST_UNCHECKED, 0);
            }
        }

        return (INT_PTR)TRUE;

    case WM_COMMAND:
        memset(cTmp, 0, sizeof(cTmp));

        switch(LOWORD(wParam))
        {
        case IDC_RADIO_SEARCH_PROCESS:
            wsprintf(cTmp, NEUZ_PROCESS);
            break;

        case IDC_RADIO_SEARCH_TITLE:
            wsprintf(cTmp, NEUZ_WINDOW);
            break;

        case IDC_RADIO_SEARCH_CLASS:
            wsprintf(cTmp, NEUZ_CLASS);
            break;
        }

        if(wcslen(cTmp) > 0)
        {
            hwndTmp = GetDlgItem(hDlg, IDC_EDIT_PROCESS);

            if(hwndTmp != NULL)
                SetWindowText(hwndTmp, cTmp);

            return (INT_PTR)TRUE;
        }

        if(LOWORD(wParam) == IDOK)
        {
            hwndTmp = GetDlgItem(hDlg, IDC_EDIT_PROCESS);

            if(hwndTmp != NULL)
            {
                memset(cTmp, 0, sizeof(cTmp));
                GetWindowText(hwndTmp, cTmp, sizeof(cTmp));

                wcscpy_s(cFindTargetName, cTmp);
            }

            for(int i = 0; i < 2; i++) // get radio buttons or checkboxe values
            {
                switch(i)
                {
                case 0:
                    nControlId = IDC_CHECK_STARTUP;
                    pSelValue = &bAutoAdd;
                    break;

                case 1:
                    nControlId = IDC_CHECK_MINIMIZE;
                    pSelValue = &bMinimize;
                    break;
                }

                hwndTmp = GetDlgItem(hDlg, nControlId);

                if(hwndTmp != NULL)
                {
                    nValue = SendMessage(hwndTmp, BM_GETSTATE, 0, 0);
                    *pSelValue = (nValue == BST_CHECKED);
                }
            }

            for(int i = 0; i < 3; i++) // get selected searchmode
            {
                switch(i)
                {
                case 0:
                    nControlId = IDC_RADIO_SEARCH_PROCESS;
                    break;

                case 1:
                    nControlId = IDC_RADIO_SEARCH_TITLE;
                    break;

                case 2:
                    nControlId = IDC_RADIO_SEARCH_CLASS;
                    break;
                }

                hwndTmp = GetDlgItem(hDlg, nControlId);

                if(hwndTmp != NULL)
                {
                    nValue = SendMessage(hwndTmp, BM_GETSTATE, 0, 0);

                    if(nValue == BST_CHECKED)
                    {
                        nSearchMode = (i + SEARCH_MODE_PROCESS);
                        break;
                    }
                }
            }

            writeConfig(bAutoAdd, bMinimize, nSearchMode, cFindTargetName);

            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        } else if(LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }

    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK ChangeTextProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND hwndTmp = NULL;
    wchar_t cTmp[MAX_TITLE_LENGTH];
    int nValue = 0;

    UNREFERENCED_PARAMETER(lParam);

    switch(message)
    {
    case WM_INITDIALOG:
        hwndTmp = GetDlgItem(hDlg, IDC_EDIT_TEXT);

        if(hwndTmp != NULL && nSelIndexText >= 0)
        {
            SetWindowText(hwndTmp, arTabs[nSelIndexText].wi.title);
        }

        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if(LOWORD(wParam) == IDOK)
        {
            hwndTmp = GetDlgItem(hDlg, IDC_EDIT_TEXT);

            if(hwndTmp != NULL)
            {
                memset(cTmp, 0, sizeof(cTmp));
                GetWindowText(hwndTmp, cTmp, sizeof(cTmp));

                delete[] arTabs[nSelIndexText].wi.title;
                arTabs[nSelIndexText].wi.title = _wcsdup(cTmp);;
                arTabs[nSelIndexText].tab.pszText = arTabs[nSelIndexText].wi.title;

                TabCtrl_SetItem(hwndTab, nSelIndexText, &arTabs[nSelIndexText].tab);
            }

            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        } else if(LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }

    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK WndAddProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND hwndTmp = NULL;
    wchar_t cTmp[MAX_TITLE_LENGTH];

    UNREFERENCED_PARAMETER(lParam);

    switch(message)
    {
    case WM_INITDIALOG:
        hwndTmp = GetDlgItem(hDlg, IDC_WND_LIST);

        if(hwndTmp != NULL)
        {
            for(UINT i = 0; i < arWnds.size(); i++)
            {
                memset(cTmp, 0, sizeof(cTmp));

                if(arWnds[i].dwPid > 0)
                    wsprintf(cTmp, L"%s (%ld)", arWnds[i].title, arWnds[i].dwPid);
                else
                    wsprintf(cTmp, L"%s", arWnds[i].title);

                ListBox_AddString(hwndTmp, cTmp);
            }
        }

        return (INT_PTR)TRUE;

    case WM_COMMAND:
        hwndTmp = GetDlgItem(hDlg, IDC_WND_LIST);

        if(hwndTmp != NULL)
        {
            if(LOWORD(wParam) == IDOK)
            {
                EndDialog(hDlg, ListBox_GetCurSel(hwndTmp));
                return (INT_PTR)TRUE;
            } else if(LOWORD(wParam) == IDCANCEL)
            {
                EndDialog(hDlg, -1);
                return (INT_PTR)TRUE;
            } else if(LOWORD(wParam) == IDC_WND_LIST && HIWORD(wParam) == LBN_DBLCLK && ListBox_GetCurSel(hwndTmp) >= 0)
            {
                EndDialog(hDlg, ListBox_GetCurSel(hwndTmp));
                return (INT_PTR)TRUE;
            }
        }

        break;
    }

    return (INT_PTR)FALSE;
}

void OnCommand(HWND hWnd, int id, HWND hwndCtl, UINT codeNotify)
{
    int nError = 0;

    switch(id)
    {
    case IDM_ABOUT:
        DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, AboutProc);
        break;

    case IDM_SETTINGS:
        DialogBox(hInst, MAKEINTRESOURCE(IDD_SETTINGS), hwndParent, SettingsProc);
        break;

    case IDM_ADD:
        DoAddWindow();
        break;

    case IDM_DELETE:
        DoDeleteWindow();
        break;

    case IDM_ADDALL:
        DoAddWindow(true);
        break;
    }
}

LRESULT OnNotify(HWND hWnd, int idCtrl, LPNMHDR pNMHdr)
{
    int nSelIndex = 0;

    switch(pNMHdr->code)
    {
    case TCN_SELCHANGING:
        nSelIndex = TabCtrl_GetCurSel(hwndTab);

        if(nSelIndex >= 0)
        {
            ShowWindow(arTabs[nSelIndex].wi.hWnd, bMinimize ? SW_FORCEMINIMIZE : SW_HIDE);
        }

        return FALSE;

    case TCN_SELCHANGE:
        nSelIndex = TabCtrl_GetCurSel(hwndTab);

        if(nSelIndex >= 0)
        {
            ShowWindow(arTabs[nSelIndex].wi.hWnd, SW_RESTORE);
            ShowWindow(arTabs[nSelIndex].wi.hWnd, SW_SHOWNA);
            //EnableWindow(arTabs[nSelIndex].wi.hWnd, false);
            SetForegroundWindow(arTabs[nSelIndex].wi.hWnd);
        }

        break;
    }

    return TRUE;
}

void OnDestroy(HWND hWnd)
{
    bQuit = true;

    while(bQuit)
    {
        Sleep(WATCHDOG_INTERVAL);
    }

    removeAllWindows();

    PostQuitMessage(0);
}

int writeConfig(bool bAutoAdd, bool bMinimize, int nSearchMode, wchar_t *cFindTargetName)
{
    int nRet = -1;
    FILE *fTmp = NULL;
    wchar_t cTmp[MAX_PATH];

    fopen_s(&fTmp, CFG_FILE, "w");

    if(fTmp != NULL)
    {
        memset(cTmp, 0, sizeof(cTmp));
        cTmp[0] = (0 | (bAutoAdd ? 0x01 : 0) | (bMinimize ? 0x02 : 0));
        fwrite(cTmp, sizeof(wchar_t), 2, fTmp);

        memset(cTmp, 0, sizeof(cTmp));
        cTmp[0] = nSearchMode;
        fwrite(cTmp, sizeof(wchar_t), 2, fTmp);

        fwrite(cFindTargetName, sizeof(wchar_t), wcslen(cFindTargetName), fTmp);

        fclose(fTmp);
        nRet = 0;
    }

    return nRet;
}

int readConfig()
{
    FILE *fTmp = NULL;
    wchar_t cTmp[MAX_PATH * sizeof(wchar_t)];

    bAutoAdd = false;
    bMinimize = false;
    nSearchMode = SEARCH_MODE_PROCESS;
    memset(cFindTargetName, 0, sizeof(cFindTargetName));
    wsprintf(cFindTargetName, NEUZ_PROCESS);

    fopen_s(&fTmp, CFG_FILE, "rb");

    if(fTmp != NULL)
    {
        memset(cTmp, 0, sizeof(cTmp));

        if(fread_s(cTmp, sizeof(cTmp), sizeof(wchar_t), 2, fTmp) > 0)
        {
            if(cTmp[0] & 0x01)
                bAutoAdd = true;

            if(cTmp[0] & 0x02)
                bMinimize = true;
        }

        if(fread_s(cTmp, sizeof(cTmp), sizeof(wchar_t), 2, fTmp) > 0)
        {
            nSearchMode = cTmp[0];
        }

        if(fread_s(cTmp, sizeof(cTmp), sizeof(wchar_t), sizeof(cTmp), fTmp) > 0)
        {
            wcscpy_s(cFindTargetName, cTmp);
        }

        fclose(fTmp);
    } else
    {
        return 1;
    }

    return 0;
}

void removeAllWindows()
{
    mtxTab.lock();

    for(UINT i = 0; i < arTabs.size(); i++)
    {
        if(TabCtrl_GetItemCount(hwndTab) >= (int)(i + 1))
        {
            TabCtrl_DeleteItem(hwndTab, i);
        }

        restoreWindow(&arTabs[i].wi);

        delete[] arTabs[i].wi.title;
    }

    mtxTab.unlock();
}

void restoreWindow(struct wndinfo *wi)
{
    for(;;)
    {
        if(IsWindow(wi->hWnd))
        {
            ShowWindow(wi->hWnd, SW_RESTORE);
            ShowWindow(wi->hWnd, SW_SHOWNA); // when change options during runtime, you never know
        } else
            break;

        if(IsWindow(wi->hWnd))
            SetWindowLong(wi->hWnd, GWL_STYLE, wi->lStyle);
        else
            break;

        if(IsWindow(wi->hWnd))
            SetParent(wi->hWnd, NULL);
        else
            break;

        if(IsWindow(wi->hWnd))
            SetWindowPos(wi->hWnd, NULL,
                wi->rcSize.left, wi->rcSize.top,
                wi->rcSize.right - wi->rcSize.left, wi->rcSize.bottom - wi->rcSize.top,
                0);
        else
            break;

        if(IsWindow(wi->hWnd))
        {
            ShowWindow(wi->hWnd, SW_RESTORE);
            ShowWindow(wi->hWnd, SW_SHOWNA);
        } else
            break;

        if(IsWindow(wi->hWnd))
            SetWindowPos(wi->hWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        else
            break;

        break;
    }
}

void selectFirstTab()
{
    NMHDR nmh;

    if(TabCtrl_GetItemCount(hwndTab) > 0)
    {
        nmh.hwndFrom = hwndTab;
        nmh.idFrom = 0;
        nmh.code = TCN_SELCHANGING;

        TabCtrl_SetCurSel(hwndTab, 0);
        nmh.code = TCN_SELCHANGE;
        SendMessage(hwndTab, WM_NOTIFY, nmh.idFrom, (LPARAM)&nmh);
    } else
    {
        SetWindowPos(hwndParent, NULL, 0, 0, WND_WIDTH, WND_HEIGHT, SWP_NOMOVE);
    }
}

void WatchdogThread()
{
    int nSelIndex = 0;
    HWND hWndLastFocus = NULL;
    bool bCheckForeground = false;

    while(!bQuit)
    {
        if(hWndLastFocus != GetActiveWindow())
        {
            bCheckForeground = true;
            hWndLastFocus = GetActiveWindow();
        }

        for(UINT i = 0; i < arTabs.size(); i++)
        {
            if(!IsWindow(arTabs[i].wi.hWnd))
            {
                mtxTab.lock();

                nSelIndex = TabCtrl_GetCurSel(hwndTab);

                TabCtrl_DeleteItem(hwndTab, i);

                delete[] arTabs[i].wi.title;
                arTabs.erase(arTabs.begin() + i);

                if(i == nSelIndex)
                {
                    selectFirstTab();
                }

                mtxTab.unlock();
                i--;
            } else if(bCheckForeground && hWndLastFocus == arTabs[i].wi.hWnd)
            {
                bCheckForeground = false;
            }
        }

        Sleep(WATCHDOG_INTERVAL);
    }

    bQuit = false;
}

void DoAddWindow(bool bAddAll, bool bAutoAdd)
{
    wndhelp wh;
    int nError = 0, nSelIndex = -1, nHeight = 0, nWidth = 0;
    RECT rcWndSize, rcWndClient, rcTabClient, rcParentSize, rcParentClient;
    long lNewStyle = 0;
    bool bSuccess = false, bResizedParent = false, bAddedTab = false;
    HWND hwndTmp = NULL;

    mtxTab.lock();
    arWnds.clear();

    EnumWindows(EnumWindowsProc, NULL);

    for(;;)
    {
        if(arWnds.size() > 0)
        {
            if(!bAddAll)
            {
                nError = DialogBox(hInst, MAKEINTRESOURCE(IDD_ADD_WINDOW), hwndParent, WndAddProc);

                if(nError >= 0)
                {
                    nSelIndex = nError;
                } else
                {
                    break;
                }
            } else
            {
                nSelIndex = 0;
            }

            if(nSelIndex >= 0)
            {
                for(UINT i = nSelIndex, x = 0; i < (bAddAll ? arWnds.size() : nSelIndex + 1); i++)
                {
                    memcpy(&wh.wi, &arWnds[i], sizeof(struct wndinfo));
                    wh.wi.title = _wcsdup(arWnds[i].title);

                    memset(&wh.tab, 0, sizeof(TCITEMW));
                    wh.tab.iImage = -1;
                    wh.tab.mask = TCIF_TEXT | TCIF_IMAGE;
                    wh.tab.pszText = wh.wi.title;

                    if(TabCtrl_InsertItem(hwndTab, TabCtrl_GetItemCount(hwndTab), &wh.tab) < 0)
                    {
                        MessageBox(hwndParent, L"Failed to add window to tabs", L"Error", MB_OK);
                        delete[] wh.wi.title;
                    } else
                    {
                        for(;;) // alle Aktionen müssen funktionieren
                        {
                            nError = 0;
                            bAddedTab = false;

                            _TRY(ShowWindow(arWnds[i].hWnd, SW_RESTORE));

                            if(!bResizedParent) // Parent resizen
                            {
                                _TRY(GetWindowRect(arWnds[i].hWnd, &rcWndSize));
                                _TRY(GetClientRect(arWnds[i].hWnd, &rcWndClient));
                                _TRY(GetWindowRect(hwndParent, &rcParentSize));
                                _TRY(GetClientRect(hwndParent, &rcParentClient));
                                _TRY(TabCtrl_GetItemRect(hwndTab, 0, &rcTabClient));

                                nHeight = (rcWndClient.bottom - rcWndClient.top);
                                nWidth = (rcWndClient.right - rcWndClient.left);

                                _TRY(SetWindowPos(hwndParent, NULL, 0, 0, // 5, 15
                                    nWidth + ((rcParentSize.right - rcParentSize.left) - (rcParentClient.right)) + GetSystemMetrics(SM_CXSIZEFRAME), // nWidth + 20
                                    nHeight + ((rcParentSize.bottom - rcParentSize.top) - (rcParentClient.bottom)) + GetSystemMetrics(SM_CYCAPTION) + GetSystemMetrics(SM_CYSIZEFRAME), // nHeight + 85
                                    SWP_NOMOVE));

                                _TRY(SetWindowPos(hwndTab, NULL, 0, 0,
                                    nWidth + (((rcParentSize.right - rcParentSize.left) - (rcParentClient.right - rcParentClient.left)) / 2) - (GetSystemMetrics(SM_CXSIZEFRAME) - GetSystemMetrics(SM_CXFIXEDFRAME)), // nWidth + 7
                                    nHeight + (((rcParentSize.bottom - rcParentSize.top) - (rcParentClient.bottom - rcParentClient.top)) / 2), // nHeigth + 29
                                    SWP_NOMOVE));

                                bResizedParent = true;
                            }

                            wh.wi.rcSize = rcWndSize;

                            arTabs.push_back(wh);
                            bAddedTab = true;

                            //lNewStyle = WS_CHILD;
                            _TRY(SetWindowLong(arWnds[i].hWnd, GWL_STYLE, lNewStyle));

                            _TRY(SetParent(arWnds[i].hWnd, hwndTab));

                            _TRY(SetWindowPos(arWnds[i].hWnd, NULL,
                                rcTabClient.left, rcTabClient.bottom + rcTabClient.top,
                                nWidth, nHeight,
                                0));

                            _TRY(ShowWindow(arWnds[i].hWnd, SW_RESTORE));

                            if((!bAddAll && arTabs.size() > 1) || (x++ > 0)) // alle anderen ausblenden
                            {
                                //_TRY(EnableWindow(arWnds[i].hWnd, false));
                                _TRY(SetForegroundWindow(arWnds[i].hWnd));
                                _TRY(ShowWindow(arWnds[i].hWnd, bMinimize ? SW_FORCEMINIMIZE : SW_HIDE));
                            } else
                            {
                                hwndTmp = arWnds[i].hWnd;
                            }

                            break;
                        }

                        if(bAddedTab && nError)
                        {
                            TabCtrl_DeleteItem(hwndTab, TabCtrl_GetItemCount(hwndTab) - 1);
                            arTabs.erase(arTabs.begin() + arTabs.size() - 1);
                            delete[] wh.wi.title;

                            continue;
                        }
                    }
                }
            }
        } else if(!bAutoAdd)
        {
            MessageBox(hwndParent, L"No windows found to add", L"Error", MB_OK);
        }

        if(hwndTmp != NULL)
        {
            //EnableWindow(hwndTmp, true);
            SetForegroundWindow(hwndTmp);
        }

        break;
    }

    for(UINT i = 0; i < arWnds.size(); i++)
    {
        delete[] arWnds[i].title;
    }

    arWnds.clear();
    mtxTab.unlock();
}

void DoDeleteWindow(int nIndex)
{
    int nSelIndex = (nIndex >= 0 ? nIndex : -1);

    mtxTab.lock();

    if(nSelIndex >= 0 || (TabCtrl_GetItemCount(hwndTab) > 0 && (nSelIndex = TabCtrl_GetCurSel(hwndTab)) >= 0))
    {
        if(arTabs.size() >= (UINT)(nSelIndex + 1))
        {
            if(TabCtrl_GetItemCount(hwndTab) >= (nSelIndex + 1))
            {
                TabCtrl_DeleteItem(hwndTab, nSelIndex);
            }

            restoreWindow(&arTabs[nSelIndex].wi);

            delete[] arTabs[nSelIndex].wi.title;
            arTabs.erase(arTabs.begin() + nSelIndex);

            selectFirstTab();
        }
    }

    mtxTab.unlock();
}

BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam)
{
    wchar_t cBuffer[MAX_PATH];
    DWORD dwPid = 0;
    HANDLE hProcess = NULL;
    struct wndinfo wi;
    long lExStyle = 0;
    bool bMatchingWnd = false;
    RECT rcClient;

    GetWindowThreadProcessId(hWnd, &dwPid); // for display in add-window dialog

    if((nSearchMode == SEARCH_MODE_PROCESS) && (dwPid <= 0))
    {
        return TRUE;
    }

    if(nSearchMode == SEARCH_MODE_PROCESS)
    {
        hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, dwPid);

        if(hProcess != NULL)
        {
            bMatchingWnd = ((GetModuleFileNameEx(hProcess, NULL, cBuffer, sizeof(cBuffer)) > 0) && !_wcsicmp(cBuffer, cFindTargetName));
            CloseHandle(hProcess);
        } else
        {
            return TRUE;
        }
    } else if(nSearchMode == SEARCH_MODE_WND_TITLE)
    {
        bMatchingWnd = (/*IsWindow(hWnd) && IsWindowVisible(hWnd) &&*/ (GetWindowText(hWnd, cBuffer, sizeof(cBuffer)) > 0) && !_wcsicmp(cBuffer, cFindTargetName));
    } else if(nSearchMode == SEARCH_MODE_WND_CLASS)
    {
        bMatchingWnd = (((RealGetWindowClass(hWnd, cBuffer, sizeof(cBuffer)) > 0) && !_wcsicmp(cBuffer, cFindTargetName)) || 
            ((GetClassName(hWnd, cBuffer, sizeof(cBuffer)) > 0) && !_wcsicmp(cBuffer, cFindTargetName)));
    }

    if(bMatchingWnd)
    {
        GetClientRect(hWnd, &rcClient);

        if((rcClient.right - rcClient.left) >= nScrWidth &&
            (rcClient.bottom - rcClient.top) >= nScrHeight) // fullscreen window (borderless)
        {
            return TRUE;
        }

        lExStyle = GetWindowExStyle(hWnd);

        if(lExStyle & WS_EX_TOPMOST) // fullscreen window
        {
            return TRUE;
        }

        for(UINT i = 0; i < arWnds.size(); i++)
        {
            if(arWnds[i].dwPid == dwPid || arWnds[i].hWnd == hWnd)
            {
                bMatchingWnd = false;
                break;
            }
        }

        if(bMatchingWnd)
        {
            memset(&wi, 0, sizeof(struct wndinfo));
            wi.hWnd = hWnd;
            wi.dwPid = dwPid;
            wi.title = _wcsdup(cBuffer);
            wi.lStyle = GetWindowLong(hWnd, GWL_STYLE);

            arWnds.push_back(wi);
        }
    }

    return TRUE;
}
