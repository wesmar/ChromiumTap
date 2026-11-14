// resource.h - Unified resource identifiers, command constants and localized string IDs
#pragma once

// Main Application Resources
#define IDI_ICON1                       101
#define IDR_MAINICON                    102
#define IDR_PASSEXTRACTOR_VERSION       202
#define IDR_WALLPAPER_BMP               2001

// Menu and Command IDs
#define IDM_FILE_OPEN_OUTPUT            1001
#define IDM_FILE_BROWSE_OUTPUT          1002
#define IDM_FILE_EXIT                   1003

#define IDM_EXTRACT_ALL                 1010
#define IDM_EXTRACT_CHROME              1011
#define IDM_EXTRACT_EDGE                1012
#define IDM_EXTRACT_BRAVE               1013
#define IDM_EXTRACT_KILL_LOCKS          1014
#define IDM_EXTRACT_FIX_TYPELIB         1015

#define IDM_VIEW_VERBOSE                1020
#define IDM_VIEW_CLEAR_LOG              1021
#define IDM_VIEW_COPY_LOG               1022
#define IDM_VIEW_REFRESH                1023

#define IDM_HELP_CLI                    1030
#define IDM_HELP_ABOUT                  1031

#define IDM_LANGUAGE_AUTO               1100
#define IDM_LANGUAGE_FIRST              1101
#define IDM_LANGUAGE_LAST               1199

// Control IDs
#define IDC_TARGET_ALL                  2001
#define IDC_TARGET_CHROME               2002
#define IDC_TARGET_EDGE                 2003
#define IDC_TARGET_BRAVE                2004

#define IDC_BTN_EXTRACT                 2010
#define IDC_BTN_CANCEL                  2011
#define IDC_BTN_BROWSE                  2012
#define IDC_BTN_OPEN_OUTPUT             2013
#define IDC_CHK_VERBOSE                 2014

#define IDC_LOG_VIEW                    2020

// Localized String IDs (STRINGTABLE & language\*.lng)
// General
#define IDS_APP_TITLE                   10000
#define IDS_LANGUAGE_AUTO               10001
#define IDS_OK                          10002
#define IDS_CLOSE                       10003
#define IDS_COPY                        10004
#define IDS_CANCEL                      10005

// Top-level menus
#define IDS_MENU_FILE                   11000
#define IDS_MENU_EXTRACTION             11001
#define IDS_MENU_VIEW                   11002
#define IDS_MENU_HELP                   11003

// File menu items
#define IDS_MI_SELECT_OUTPUT            11010
#define IDS_MI_OPEN_OUTPUT              11011
#define IDS_MI_EXIT                     11012

// Extraction menu items
#define IDS_MI_START_EXTRACT            11020
#define IDS_MI_STOP_EXTRACT             11021
#define IDS_MI_TARGET_ALL               11022
#define IDS_MI_TARGET_CHROME            11023
#define IDS_MI_TARGET_EDGE              11024
#define IDS_MI_TARGET_BRAVE             11025
#define IDS_MI_FIX_TYPELIB              11026
#define IDS_MI_KILL_LOCKS               11027

// View menu items
#define IDS_MI_VERBOSE                  11030
#define IDS_MI_COPY_LOG                 11031
#define IDS_MI_CLEAR_LOG                11032
#define IDS_MI_REFRESH_BROWSERS         11033
#define IDS_MI_LANGUAGE                 11034

// Help menu items
#define IDS_MI_HELP_CLI                 11040
#define IDS_MI_ABOUT                    11041

// Main window cards & buttons
#define IDS_CARD_TARGETS                12000
#define IDS_CARD_OUTPUT_LABEL           12001
#define IDS_BTN_ALL_BROWSERS            12002
#define IDS_BTN_CHROME                  12003
#define IDS_BTN_EDGE                    12004
#define IDS_BTN_BRAVE                   12005
#define IDS_BTN_START_EXTRACT           12006
#define IDS_BTN_STOP                    12007
#define IDS_BTN_VERBOSE                 12008
#define IDS_BTN_BROWSE                  12009
#define IDS_BTN_OPEN_FOLDER             12010

// Statistics KPI labels
#define IDS_STAT_PASSWORDS              12020
#define IDS_STAT_COOKIES                12021
#define IDS_STAT_CARDS                  12022
#define IDS_STAT_PROFILES               12023

// Status & log messages
#define IDS_STATUS_READY                13000
#define IDS_STATUS_EXTRACTING           13001
#define IDS_STATUS_STOPPING             13002
#define IDS_STATUS_DONE_OK              13003
#define IDS_STATUS_DONE_ISSUES          13004
#define IDS_STATUS_TARGET_SELECTED      13005
#define IDS_STATUS_OUTPUT_UPDATED       13006
#define IDS_STATUS_LOCKS_TERMINATED     13007
#define IDS_STATUS_TYPELIB_FIXED        13008
#define IDS_STATUS_TYPELIB_WARN         13009
#define IDS_LOG_INIT_MICA               13010
#define IDS_LOG_SYSCALLS_ARMED          13011
#define IDS_LOG_OUTPUT_CONFIGURED       13012
#define IDS_LOG_DETECTED_BROWSERS       13013
#define IDS_LOG_LANGUAGE_CHANGED        13014

// Dialogs (About & CLI Help)
#define IDS_HELP_HEADING                14000
#define IDS_HELP_MESSAGE                14001
#define IDS_ABOUT_HEADING               14002
#define IDS_ABOUT_MESSAGE               14003
#define IDS_HELP_USAGE                  14004

// Extraction-engine diagnostic events (message IDs travel through the named pipe)
#define IDS_EVENT_EXTRACTION_STARTED        15000
#define IDS_EVENT_EDGE_COM_ATTEMPT          15001
#define IDS_EVENT_EDGE_COM_SUCCESS          15002
#define IDS_EVENT_EDGE_COM_FAILED           15003
#define IDS_EVENT_EDGE_DPAPI_FALLBACK       15004
#define IDS_EVENT_EDGE_NO_KEY               15005
#define IDS_EVENT_SINGLE_KEY_START          15006
#define IDS_EVENT_SINGLE_KEY_FAILED         15007
#define IDS_EVENT_SINGLE_KEY_SUCCESS        15008
#define IDS_EVENT_DATABASE_HANDSHAKE_DONE   15009
#define IDS_EVENT_PROFILES_FOUND            15010
#define IDS_EVENT_PROCESSING_PROFILE        15011
#define IDS_EVENT_USING_COM_KEY             15012
#define IDS_EVENT_EXTRACTION_COMPLETE       15013
#define IDS_EVENT_READ_VERBOSE_FAILED       15014
#define IDS_EVENT_READ_OUTPUT_FAILED        15015
#define IDS_EVENT_OUTPUT_CONFIGURED         15016
#define IDS_EVENT_READ_DPAPI_FAILED         15017
#define IDS_EVENT_DPAPI_NOT_PROVIDED        15018
#define IDS_EVENT_DPAPI_RECEIVED            15019
#define IDS_EVENT_DPAPI_INVALID             15020
#define IDS_EVENT_CRITICAL_MODULE_ERROR     15021
#define IDS_EVENT_DISCOVERING_PROFILES      15022
#define IDS_EVENT_DATABASE_OPEN_FAILED      15023
#define IDS_EVENT_DATABASE_PREPARE_FAILED   15024
#define IDS_EVENT_OUTPUT_DIRECTORY_FAILED   15025
#define IDS_EVENT_DATA_EXTRACTED            15026
#define IDS_EVENT_READING_LOCAL_STATE       15027
#define IDS_EVENT_USING_COM_ELEVATION       15028
#define IDS_EVENT_COM_MODE_CHANGED          15029
#define IDS_EVENT_COM_INITIALIZED           15030
#define IDS_EVENT_REQUESTING_MASTER_KEY     15031
#define IDS_EVENT_COM_KEY_DECRYPTED         15032
#define IDS_EVENT_COM_PROXY_FAILED          15033
#define IDS_EVENT_COM_DECRYPT_FAILED        15034
#define IDS_EVENT_DPAPI_DECRYPT_FAILED      15035
#define IDS_EVENT_DPAPI_SIZE_MISMATCH       15036
#define IDS_EVENT_COM_OPERATION_DEBUG       15037
#define IDS_EVENT_GENERIC_ERROR             15038
#define IDS_EVENT_DATABASE_HANDSHAKE_FAILED 15039

#define IDS_EVENT_WORKER_SYSCALL_INIT        15100
#define IDS_EVENT_WORKER_SYSCALL_FAILED      15101
#define IDS_EVENT_WORKER_SQLITE_FALLBACK     15102
#define IDS_EVENT_WORKER_SQLITE_MISSING      15103
#define IDS_EVENT_WORKER_MODULE_MISSING      15104
#define IDS_EVENT_WORKER_STARTING_ALL        15105
#define IDS_EVENT_WORKER_NO_BROWSERS         15106
#define IDS_EVENT_WORKER_PROCESSING_BROWSER  15107
#define IDS_EVENT_WORKER_BROWSER_EXE_MISSING 15108
#define IDS_EVENT_WORKER_BROWSER_COMPLETE    15109
#define IDS_EVENT_WORKER_BROWSER_FAILED      15110
#define IDS_EVENT_WORKER_TARGET_EXECUTABLE   15111
#define IDS_EVENT_SERVER_MODULE_ENDED        15112
#define IDS_EVENT_EDGE_DPAPI_SUCCESS         15113
#define IDS_EVENT_ORCH_STARTING_ALL          15114
#define IDS_EVENT_ORCH_NO_BROWSERS           15115
#define IDS_EVENT_ORCH_PROCESSING_COUNT      15116
#define IDS_EVENT_ORCH_COMPLETED_SUMMARY     15117
#define IDS_EVENT_ORCH_PROCESSING_BROWSER    15118
#define IDS_EVENT_NO_DATA_EXTRACTED          15119
#define IDS_EVENT_OUTPUT_STORED              15120
#define IDS_EVENT_EXTRACTION_SUMMARY         15121
