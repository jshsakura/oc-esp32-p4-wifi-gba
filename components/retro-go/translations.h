#include "rg_localization.h"

static const char *language_names[RG_LANG_MAX] = {
    [RG_LANG_EN] = "English",
    [RG_LANG_ZH] = "简体中文",
    [RG_LANG_KO] = "한국어",
};

// Korean is filled in as it is written rather than all at once. An entry with no
// [RG_LANG_KO] is not a hole: rg_gettext returns the original string when a translation is
// missing, so anything untranslated simply shows in English. That makes it safe to add
// these a handful at a time.
//
// The strings were not machine-derived from the Game & Watch fork. Joining the two projects
// on their string keys covers only 15% and gets some of that wrong -- its language-name
// field maps "English" to the name of whatever language you picked, so every "English" in
// the UI would have become "한국어".

static const char *translations[][RG_LANG_MAX] =
{
    {
        [RG_LANG_EN] = "Auto-save",
        [RG_LANG_ZH] = "自动保存",
        [RG_LANG_KO] = "자동 저장",
    },
    {
        [RG_LANG_EN] = "Never",
        [RG_LANG_ZH] = "从不",
        [RG_LANG_KO] = "안 함",
    },
    {
        [RG_LANG_EN] = "Always",
        [RG_LANG_ZH] = "总是",
        [RG_LANG_KO] = "항상",
    },
    {
        [RG_LANG_EN] = "Composite",
        [RG_LANG_ZH] = "复合",
        [RG_LANG_KO] = "컴포지트",
    },
    {
        [RG_LANG_EN] = "NES Classic",
        [RG_LANG_ZH] = "NES经典",
        [RG_LANG_KO] = "NES Classic",
    },
    {
        [RG_LANG_EN] = "NTSC",
        [RG_LANG_ZH] = "NTSC",
        [RG_LANG_KO] = "NTSC",
    },
    {
        [RG_LANG_EN] = "PVM",
        [RG_LANG_ZH] = "PVM",
        [RG_LANG_KO] = "PVM",
    },
    {
        [RG_LANG_EN] = "Smooth",
        [RG_LANG_ZH] = "平滑",
        [RG_LANG_KO] = "부드럽게",
    },
    {
        [RG_LANG_EN] = "To start, try: 1 or * or #",
        [RG_LANG_ZH] = "开始请按: 1 或 * 或 #",
        [RG_LANG_KO] = "시작하려면 1, * 또는 # 을 눌러보세요",
    },
    {
        [RG_LANG_EN] = "Full",
        [RG_LANG_ZH] = "全屏",
        [RG_LANG_KO] = "가득 채우기",
    },
    {
        [RG_LANG_EN] = "Yes",
        [RG_LANG_ZH] = "是",
        [RG_LANG_KO] = "예",
    },
    {
        [RG_LANG_EN] = "Select file",
        [RG_LANG_ZH] = "选择文件",
        [RG_LANG_KO] = "파일 선택",
    },
    {
        [RG_LANG_EN] = "Language",
        [RG_LANG_ZH] = "语言",
        [RG_LANG_KO] = "언어",
    },
    {
        [RG_LANG_EN] = "Language changed!",
        [RG_LANG_ZH] = "语言已更改!",
        [RG_LANG_KO] = "언어를 변경했습니다!",
    },
    {
        [RG_LANG_EN] = "For these changes to take effect you must restart your device.\nrestart now?",
        [RG_LANG_ZH] = "更改需要重启设备才能生效。\n立即重启?",
        [RG_LANG_KO] = "변경 사항을 적용하려면 기기를 다시 시작해야 합니다.\n지금 다시 시작할까요?",
    },
    {
        [RG_LANG_EN] = "Wi-Fi profile",
        [RG_LANG_ZH] = "Wi-Fi配置",
        [RG_LANG_KO] = "Wi-Fi 프로필",
    },
    {
        [RG_LANG_EN] = "Options",
        [RG_LANG_ZH] = "选项",
        [RG_LANG_KO] = "설정",
    },
    {
        [RG_LANG_EN] = "About Retro-Go",
        [RG_LANG_ZH] = "关于Retro-Go",
        [RG_LANG_KO] = "Retro-Go 정보",
    },
    {
        [RG_LANG_EN] = "Reset all settings?",
        [RG_LANG_ZH] = "重置所有设置?",
        [RG_LANG_KO] = "모든 설정을 초기화할까요?",
    },
    {
        [RG_LANG_EN] = "Initializing...",
        [RG_LANG_ZH] = "初始化中...",
        [RG_LANG_KO] = "초기화 중...",
    },
    {
        [RG_LANG_EN] = "Host Game (P1)",
        [RG_LANG_ZH] = "主机游戏 (P1)",
        [RG_LANG_KO] = "게임 열기 (P1)",
    },
    {
        [RG_LANG_EN] = "Find Game (P2)",
        [RG_LANG_ZH] = "寻找游戏 (P2)",
        [RG_LANG_KO] = "게임 찾기 (P2)",
    },
    {
        [RG_LANG_EN] = "Netplay",
        [RG_LANG_ZH] = "联机游戏",
        [RG_LANG_KO] = "넷플레이",
    },
    {
        [RG_LANG_EN] = "ROMs not identical. Continue?",
        [RG_LANG_ZH] = "ROM不匹配。继续?",
        [RG_LANG_KO] = "ROM이 서로 다릅니다. 계속할까요?",
    },
    {
        [RG_LANG_EN] = "Exchanging info...",
        [RG_LANG_ZH] = "交换信息中...",
        [RG_LANG_KO] = "정보 교환 중...",
    },
    {
        [RG_LANG_EN] = "Unable to find host!",
        [RG_LANG_ZH] = "无法找到主机!",
        [RG_LANG_KO] = "호스트를 찾을 수 없습니다!",
    },
    {
        [RG_LANG_EN] = "Connection failed!",
        [RG_LANG_ZH] = "连接失败!",
        [RG_LANG_KO] = "연결에 실패했습니다!",
    },
    {
        [RG_LANG_EN] = "Waiting for peer...",
        [RG_LANG_ZH] = "等待对手...",
        [RG_LANG_KO] = "상대를 기다리는 중...",
    },
    {
        [RG_LANG_EN] = "Unknown status...",
        [RG_LANG_ZH] = "未知状态...",
        [RG_LANG_KO] = "알 수 없는 상태...",
    },
    {
        [RG_LANG_EN] = "On",
        [RG_LANG_ZH] = "开",
        [RG_LANG_KO] = "켜기",
    },
    {
        [RG_LANG_EN] = "Keyboard",
        [RG_LANG_ZH] = "键盘",
        [RG_LANG_KO] = "키보드",
    },
    {
        [RG_LANG_EN] = "Joystick",
        [RG_LANG_ZH] = "摇杆",
        [RG_LANG_KO] = "조이스틱",
    },
    {
        [RG_LANG_EN] = "Input",
        [RG_LANG_ZH] = "输入",
        [RG_LANG_KO] = "입력",
    },
    {
        [RG_LANG_EN] = "Crop",
        [RG_LANG_ZH] = "裁剪",
        [RG_LANG_KO] = "잘라내기",
    },
    {
        [RG_LANG_EN] = "BIOS file missing!",
        [RG_LANG_ZH] = "BIOS文件缺失!",
        [RG_LANG_KO] = "BIOS 파일이 없습니다!",
    },
    {
        [RG_LANG_EN] = "YM2612 audio ",
        [RG_LANG_ZH] = "YM2612音频",
        [RG_LANG_KO] = "YM2612 사운드",
    },
    {
        [RG_LANG_EN] = "SN76489 audio",
        [RG_LANG_ZH] = "SN76489音频",
        [RG_LANG_KO] = "SN76489 사운드",
    },
    {
        [RG_LANG_EN] = "Z80 emulation",
        [RG_LANG_ZH] = "Z80模拟",
        [RG_LANG_KO] = "Z80 에뮬레이션",
    },
    {
        [RG_LANG_EN] = "Launcher options",
        [RG_LANG_ZH] = "启动器选项",
        [RG_LANG_KO] = "런처 설정",
    },
    {
        [RG_LANG_EN] = "Emulator options",
        [RG_LANG_ZH] = "模拟器选项",
        [RG_LANG_KO] = "에뮬레이터 설정",
    },
    {
        [RG_LANG_EN] = "Date",
        [RG_LANG_ZH] = "日期",
        [RG_LANG_KO] = "날짜",
    },
    {
        [RG_LANG_EN] = "Files:",
        [RG_LANG_ZH] = "文件:",
        [RG_LANG_KO] = "파일:",
    },
    {
        [RG_LANG_EN] = "Download complete!",
        [RG_LANG_ZH] = "下载完成!",
        [RG_LANG_KO] = "다운로드 완료!",
    },
    {
        [RG_LANG_EN] = "Reboot to flash?",
        [RG_LANG_ZH] = "重启以刷写?",
        [RG_LANG_KO] = "플래시하려면 재시작할까요?",
    },
    {
        [RG_LANG_EN] = "Available Releases",
        [RG_LANG_ZH] = "可用版本",
        [RG_LANG_KO] = "사용 가능한 릴리스",
    },
    {
        [RG_LANG_EN] = "Received empty list!",
        [RG_LANG_ZH] = "收到空列表!",
        [RG_LANG_KO] = "받은 목록이 비어 있습니다!",
    },
    {
        [RG_LANG_EN] = "Gamma Boost",
        [RG_LANG_ZH] = "伽马增强",
        [RG_LANG_KO] = "감마 보정",
    },
    {
        [RG_LANG_EN] = "Day",
        [RG_LANG_ZH] = "日",
        [RG_LANG_KO] = "일",
    },
    {
        [RG_LANG_EN] = "Hour",
        [RG_LANG_ZH] = "时",
        [RG_LANG_KO] = "시",
    },
    {
        [RG_LANG_EN] = "Min",
        [RG_LANG_ZH] = "分",
        [RG_LANG_KO] = "분",
    },
    {
        [RG_LANG_EN] = "Sec",
        [RG_LANG_ZH] = "秒",
        [RG_LANG_KO] = "초",
    },
    {
        [RG_LANG_EN] = "Sync",
        [RG_LANG_ZH] = "同步",
        [RG_LANG_KO] = "동기화",
    },
    {
        [RG_LANG_EN] = "RTC config",
        [RG_LANG_ZH] = "RTC设置",
        [RG_LANG_KO] = "시계 설정",
    },
    {
        [RG_LANG_EN] = "Palette",
        [RG_LANG_ZH] = "调色板",
        [RG_LANG_KO] = "팔레트",
    },
    {
        [RG_LANG_EN] = "SRAM autosave",
        [RG_LANG_ZH] = "SRAM自动保存",
        [RG_LANG_KO] = "SRAM 자동 저장",
    },
    {
        [RG_LANG_EN] = "Enable BIOS",
        [RG_LANG_ZH] = "启用BIOS",
        [RG_LANG_KO] = "BIOS 사용",
    },
    {
        [RG_LANG_EN] = "Name",
        [RG_LANG_ZH] = "名称",
        [RG_LANG_KO] = "이름",
    },
    {
        [RG_LANG_EN] = "Artist",
        [RG_LANG_ZH] = "艺术家",
        [RG_LANG_KO] = "아티스트",
    },
    {
        [RG_LANG_EN] = "Copyright",
        [RG_LANG_ZH] = "版权",
        [RG_LANG_KO] = "저작권",
    },
    {
        [RG_LANG_EN] = "Playing",
        [RG_LANG_ZH] = "播放中",
        [RG_LANG_KO] = "재생 중",
    },
    {
        [RG_LANG_EN] = "Overscan",
        [RG_LANG_ZH] = "过扫描",
        [RG_LANG_KO] = "오버스캔",
    },
    {
        [RG_LANG_EN] = "Crop sides",
        [RG_LANG_ZH] = "裁剪边缘",
        [RG_LANG_KO] = "좌우 잘라내기",
    },
    {
        [RG_LANG_EN] = "Sprite Limit",
        [RG_LANG_ZH] = "精灵限制",
        [RG_LANG_KO] = "스프라이트 제한",
    },
    {
        [RG_LANG_EN] = "Profile",
        [RG_LANG_ZH] = "配置",
        [RG_LANG_KO] = "프로필",
    },
    {
        [RG_LANG_EN] = "<profile name>",
        [RG_LANG_ZH] = "<配置名称>",
        [RG_LANG_KO] = "<프로필 이름>",
    },
    {
        [RG_LANG_EN] = "Controls",
        [RG_LANG_ZH] = "控制",
        [RG_LANG_KO] = "조작",
    },
    {
        [RG_LANG_EN] = "Audio enable",
        [RG_LANG_ZH] = "启用音频",
        [RG_LANG_KO] = "사운드 사용",
    },
    {
        [RG_LANG_EN] = "Audio filter",
        [RG_LANG_ZH] = "音频滤波",
        [RG_LANG_KO] = "사운드 필터",
    },

    // rg_gui.c
    {
        [RG_LANG_EN] = "Folder is empty.",
        [RG_LANG_ZH] = "文件夹为空。",
        [RG_LANG_KO] = "폴더가 비어 있습니다.",
    },
    {
        [RG_LANG_EN] = "No",
        [RG_LANG_ZH] = "否",
        [RG_LANG_KO] = "아니오",
    },
    {
        [RG_LANG_EN] = "OK",
        [RG_LANG_ZH] = "确定",
        [RG_LANG_KO] = "확인",
    },
    {
        [RG_LANG_EN] = "Off",
        [RG_LANG_ZH] = "关",
        [RG_LANG_KO] = "끄기",
    },
    {
        [RG_LANG_EN] = "Horiz",
        [RG_LANG_ZH] = "水平",
        [RG_LANG_KO] = "가로",
    },
    {
        [RG_LANG_EN] = "Vert",
        [RG_LANG_ZH] = "垂直",
        [RG_LANG_KO] = "세로",
    },
    {
        [RG_LANG_EN] = "Both",
        [RG_LANG_ZH] = "两者",
        [RG_LANG_KO] = "양쪽",
    },
    {
        [RG_LANG_EN] = "Fit",
        [RG_LANG_ZH] = "适应",
        [RG_LANG_KO] = "비율 맞춤",
    },
    {
        [RG_LANG_EN] = "Zoom",
        [RG_LANG_ZH] = "缩放",
        [RG_LANG_KO] = "확대",
    },

    // Led options
    {
        [RG_LANG_EN] = "LED options",
        [RG_LANG_ZH] = "LED选项",
        [RG_LANG_KO] = "LED 설정",
    },
    {
        [RG_LANG_EN] = "System activity",
        [RG_LANG_ZH] = "系统活动",
        [RG_LANG_KO] = "시스템 동작",
    },
    {
        [RG_LANG_EN] = "Disk activity",
        [RG_LANG_ZH] = "磁盘活动",
        [RG_LANG_KO] = "디스크 동작",
    },
    {
        [RG_LANG_EN] = "Low battery",
        [RG_LANG_ZH] = "电量低",
        [RG_LANG_KO] = "배터리 부족",
    },
    {
        [RG_LANG_EN] = "Default",
        [RG_LANG_ZH] = "默认",
        [RG_LANG_KO] = "기본값",
    },
    {
        [RG_LANG_EN] = "<None>",
        [RG_LANG_ZH] = "<无>",
        [RG_LANG_KO] = "<없음>",
    },

    // Wifi
    {
        [RG_LANG_EN] = "Not connected",
        [RG_LANG_ZH] = "未连接",
        [RG_LANG_KO] = "연결 안 됨",
    },
    {
        [RG_LANG_EN] = "Connecting...",
        [RG_LANG_ZH] = "连接中...",
        [RG_LANG_KO] = "연결 중...",
    },
    {
        [RG_LANG_EN] = "Disconnecting...",
        [RG_LANG_ZH] = "断开中...",
        [RG_LANG_KO] = "연결 해제 중...",
    },
    {
        [RG_LANG_EN] = "(empty)",
        [RG_LANG_ZH] = "(空)",
        [RG_LANG_KO] = "(비어 있음)",
    },
    {
        [RG_LANG_EN] = "Wi-Fi AP",
        [RG_LANG_ZH] = "Wi-Fi热点",
        [RG_LANG_KO] = "Wi-Fi AP",
    },
    {
        [RG_LANG_EN] = "Start access point?\n\nSSID: retro-go\nPassword: retro-go\n\nBrowse: http://192.168.4.1/",
        [RG_LANG_ZH] = "启动热点?\n\nSSID: retro-go\n密码: retro-go\n\n访问: http://192.168.4.1/",
        [RG_LANG_KO] = "액세스 포인트를 시작할까요?\n\nSSID: retro-go\n비밀번호: retro-go\n\n접속: http://192.168.4.1/",
    },
    {
        [RG_LANG_EN] = "Wi-Fi enable",
        [RG_LANG_ZH] = "启用Wi-Fi",
        [RG_LANG_KO] = "Wi-Fi 사용",
    },
    {
        [RG_LANG_EN] = "Wi-Fi access point",
        [RG_LANG_ZH] = "Wi-Fi热点",
        [RG_LANG_KO] = "Wi-Fi 액세스 포인트",
    },
    {
        [RG_LANG_EN] = "Network",
        [RG_LANG_ZH] = "网络",
        [RG_LANG_KO] = "네트워크",
    },
    {
        [RG_LANG_EN] = "IP address",
        [RG_LANG_ZH] = "IP地址",
        [RG_LANG_KO] = "IP 주소",
    },

    // retro-go settings
    {
        [RG_LANG_EN] = "Brightness",
        [RG_LANG_ZH] = "亮度",
        [RG_LANG_KO] = "밝기",
    },
    {
        [RG_LANG_EN] = "Volume",
        [RG_LANG_ZH] = "音量",
        [RG_LANG_KO] = "음량",
    },
    {
        [RG_LANG_EN] = "Audio out",
        [RG_LANG_ZH] = "音频输出",
        [RG_LANG_KO] = "사운드 출력",
    },
    {
        [RG_LANG_EN] = "Font type",
        [RG_LANG_ZH] = "字体类型",
        [RG_LANG_KO] = "글꼴 종류",
    },
    {
        [RG_LANG_EN] = "Theme",
        [RG_LANG_ZH] = "主题",
        [RG_LANG_KO] = "테마",
    },
    {
        [RG_LANG_EN] = "Show clock",
        [RG_LANG_ZH] = "显示时钟",
        [RG_LANG_KO] = "시계 표시",
    },
    {
        [RG_LANG_EN] = "Timezone",
        [RG_LANG_ZH] = "时区",
        [RG_LANG_KO] = "표준시",
    },
    {
        [RG_LANG_EN] = "Wi-Fi options",
        [RG_LANG_ZH] = "Wi-Fi选项",
        [RG_LANG_KO] = "Wi-Fi 설정",
    },

    // app settings
    {
        [RG_LANG_EN] = "Scaling",
        [RG_LANG_ZH] = "缩放",
        [RG_LANG_KO] = "화면 비율",
    },
    {
        [RG_LANG_EN] = "Factor",
        [RG_LANG_ZH] = "比例",
        [RG_LANG_KO] = "배율",
    },
    {
        [RG_LANG_EN] = "Filter",
        [RG_LANG_ZH] = "滤镜",
        [RG_LANG_KO] = "필터",
    },
    {
        [RG_LANG_EN] = "CRT Effect",
        [RG_LANG_ZH] = "CRT 扫描线",
        [RG_LANG_KO] = "CRT 스캔라인",
    },
    {
        [RG_LANG_EN] = "Pixel-perfect",
        [RG_LANG_ZH] = "像素完美",
        [RG_LANG_KO] = "픽셀 퍼펙트",
    },
    {
        [RG_LANG_EN] = "4:3",
        [RG_LANG_ZH] = "4:3",
        [RG_LANG_KO] = "4:3",
    },
    {
        [RG_LANG_EN] = "Border",
        [RG_LANG_ZH] = "边框",
        [RG_LANG_KO] = "테두리",
    },
    {
        [RG_LANG_EN] = "Speed",
        [RG_LANG_ZH] = "速度",
        [RG_LANG_KO] = "속도",
    },

    // about menu
    {
        [RG_LANG_EN] = "Version",
        [RG_LANG_ZH] = "版本",
        [RG_LANG_KO] = "버전",
    },
    {
        [RG_LANG_EN] = "Target",
        [RG_LANG_ZH] = "目标设备",
        [RG_LANG_KO] = "타겟",
    },
    {
        [RG_LANG_EN] = "Website",
        [RG_LANG_ZH] = "网站",
        [RG_LANG_KO] = "웹사이트",
    },
    {
        [RG_LANG_EN] = "Debug menu",
        [RG_LANG_ZH] = "调试菜单",
        [RG_LANG_KO] = "디버그 메뉴",
    },
    {
        [RG_LANG_EN] = "Reset settings",
        [RG_LANG_ZH] = "重置设置",
        [RG_LANG_KO] = "설정 초기화",
    },

    // save slot
    {
        [RG_LANG_EN] = "Slot 0",
        [RG_LANG_ZH] = "存档位 0",
        [RG_LANG_KO] = "슬롯 0",
    },
    {
        [RG_LANG_EN] = "Slot 1",
        [RG_LANG_ZH] = "存档位 1",
        [RG_LANG_KO] = "슬롯 1",
    },
    {
        [RG_LANG_EN] = "Slot 2",
        [RG_LANG_ZH] = "存档位 2",
        [RG_LANG_KO] = "슬롯 2",
    },
    {
        [RG_LANG_EN] = "Slot 3",
        [RG_LANG_ZH] = "存档位 3",
        [RG_LANG_KO] = "슬롯 3",
    },

    // game menu
    {
        [RG_LANG_EN] = "Save & Continue",
        [RG_LANG_ZH] = "保存并继续",
        [RG_LANG_KO] = "저장 후 계속",
    },
    {
        [RG_LANG_EN] = "Save & Quit",
        [RG_LANG_ZH] = "保存并退出",
        [RG_LANG_KO] = "저장 후 종료",
    },
    {
        [RG_LANG_EN] = "Load game",
        [RG_LANG_ZH] = "加载游戏",
        [RG_LANG_KO] = "게임 불러오기",
    },
    {
        [RG_LANG_EN] = "Reset",
        [RG_LANG_ZH] = "重置",
        [RG_LANG_KO] = "초기화",
    },
    {
        [RG_LANG_EN] = "About",
        [RG_LANG_ZH] = "关于",
        [RG_LANG_KO] = "정보",
    },
    {
        [RG_LANG_EN] = "Quit",
        [RG_LANG_ZH] = "退出",
        [RG_LANG_KO] = "종료",
    },
    {
        [RG_LANG_EN] = "Soft reset",
        [RG_LANG_ZH] = "软复位",
        [RG_LANG_KO] = "소프트 리셋",
    },
    {
        [RG_LANG_EN] = "Hard reset",
        [RG_LANG_ZH] = "硬复位",
        [RG_LANG_KO] = "하드 리셋",
    },
    {
        [RG_LANG_EN] = "Reset Emulation?",
        [RG_LANG_ZH] = "重置模拟?",
        [RG_LANG_KO] = "에뮬레이션을 초기화할까요?",
    },
    {
        [RG_LANG_EN] = "Save",
        [RG_LANG_ZH] = "保存",
        [RG_LANG_KO] = "저장",
    },
    {
        [RG_LANG_EN] = "Load",
        [RG_LANG_ZH] = "加载",
        [RG_LANG_KO] = "불러오기",
    },
    // end of rg_gui.c

    // main.c
    {
        [RG_LANG_EN] = "Show",
        [RG_LANG_ZH] = "显示",
        [RG_LANG_KO] = "표시",
    },
    {
        [RG_LANG_EN] = "Hide",
        [RG_LANG_ZH] = "隐藏",
        [RG_LANG_KO] = "숨김",
    },
    {
        [RG_LANG_EN] = "Tabs Visibility",
        [RG_LANG_ZH] = "标签页可见性",
        [RG_LANG_KO] = "탭 표시 설정",
    },

    // scroll modes
    {
        [RG_LANG_EN] = "Center",
        [RG_LANG_ZH] = "居中",
        [RG_LANG_KO] = "가운데",
    },
    {
        [RG_LANG_EN] = "Paging",
        [RG_LANG_ZH] = "分页",
        [RG_LANG_KO] = "페이지",
    },

    // start screen
    {
        [RG_LANG_EN] = "Auto",
        [RG_LANG_ZH] = "自动",
        [RG_LANG_KO] = "자동",
    },
    {
        [RG_LANG_EN] = "Carousel",
        [RG_LANG_ZH] = "轮播",
        [RG_LANG_KO] = "캐러셀",
    },
    {
        [RG_LANG_EN] = "Browser",
        [RG_LANG_ZH] = "浏览器",
        [RG_LANG_KO] = "탐색기",
    },

    // preview
    {
        [RG_LANG_EN] = "None",
        [RG_LANG_ZH] = "无",
        [RG_LANG_KO] = "없음",
    },
    {
        [RG_LANG_EN] = "Cover,Save",
        [RG_LANG_ZH] = "封面,存档",
        [RG_LANG_KO] = "커버, 세이브",
    },
    {
        [RG_LANG_EN] = "Save,Cover",
        [RG_LANG_ZH] = "存档,封面",
        [RG_LANG_KO] = "세이브, 커버",
    },
    {
        [RG_LANG_EN] = "Cover only",
        [RG_LANG_ZH] = "仅封面",
        [RG_LANG_KO] = "커버만",
    },
    {
        [RG_LANG_EN] = "Save only",
        [RG_LANG_ZH] = "仅存档",
        [RG_LANG_KO] = "세이브만",
    },

    // startup app
    {
        [RG_LANG_EN] = "Last game",
        [RG_LANG_ZH] = "上次游戏",
        [RG_LANG_KO] = "마지막 게임",
    },
    {
        [RG_LANG_EN] = "Launcher",
        [RG_LANG_ZH] = "启动器",
        [RG_LANG_KO] = "런처",
    },

    // launcher options
    {
        [RG_LANG_EN] = "Launcher Options",
        [RG_LANG_ZH] = "启动器选项",
        [RG_LANG_KO] = "런처 설정",
    },
    {
        [RG_LANG_EN] = "Color theme",
        [RG_LANG_ZH] = "颜色主题",
        [RG_LANG_KO] = "색 테마",
    },
    {
        [RG_LANG_EN] = "Preview",
        [RG_LANG_ZH] = "预览",
        [RG_LANG_KO] = "미리보기",
    },
    {
        [RG_LANG_EN] = "Scroll mode",
        [RG_LANG_ZH] = "滚动模式",
        [RG_LANG_KO] = "스크롤 방식",
    },
    {
        [RG_LANG_EN] = "Start screen",
        [RG_LANG_ZH] = "启动画面",
        [RG_LANG_KO] = "시작 화면",
    },
    {
        [RG_LANG_EN] = "Hide tabs",
        [RG_LANG_ZH] = "隐藏标签",
        [RG_LANG_KO] = "탭 숨기기",
    },
    {
        [RG_LANG_EN] = "File server",
        [RG_LANG_ZH] = "文件服务器",
        [RG_LANG_KO] = "파일 서버",
    },
    {
        [RG_LANG_EN] = "Startup app",
        [RG_LANG_ZH] = "启动应用",
        [RG_LANG_KO] = "시작 앱",
    },
    {
        [RG_LANG_EN] = "Build CRC cache",
        [RG_LANG_ZH] = "建立CRC缓存",
        [RG_LANG_KO] = "CRC 캐시 만들기",
    },
    {
        [RG_LANG_EN] = "Check for updates",
        [RG_LANG_ZH] = "检查更新",
        [RG_LANG_KO] = "업데이트 확인",
    },
    {
        [RG_LANG_EN] = "HTTP Server Busy...",
        [RG_LANG_ZH] = "HTTP服务器忙...",
        [RG_LANG_KO] = "HTTP 서버가 사용 중입니다...",
    },
    {
        [RG_LANG_EN] = "SD Card Error",
        [RG_LANG_ZH] = "SD卡错误",
        [RG_LANG_KO] = "SD 카드 오류",
    },
    {
        [RG_LANG_EN] = "Storage mount failed.\nMake sure the card is FAT32.",
        [RG_LANG_ZH] = "存储挂载失败。\n请确保卡是FAT32格式。",
        [RG_LANG_KO] = "저장소를 마운트하지 못했습니다.\n카드가 FAT32인지 확인하세요.",
    },
    // end of main.c

    // applications.c
    {
        [RG_LANG_EN] = "Scanning %s %d/%d",
        [RG_LANG_ZH] = "扫描 %s %d/%d",
        [RG_LANG_KO] = "검사 중 %s %d/%d",
    },
    {
        [RG_LANG_EN] = "Welcome to Retro-Go!",
        [RG_LANG_ZH] = "欢迎使用Retro-Go!",
        [RG_LANG_KO] = "Retro-Go에 오신 것을 환영합니다!",
    },
    {
        [RG_LANG_EN] = "Place roms in folder: %s",
        [RG_LANG_ZH] = "将ROM放入文件夹: %s",
        [RG_LANG_KO] = "ROM을 이 폴더에 넣으세요: %s",
    },
    {
        [RG_LANG_EN] = "With file extension: %s",
        [RG_LANG_ZH] = "文件扩展名: %s",
        [RG_LANG_KO] = "파일 확장자: %s",
    },
    {
        [RG_LANG_EN] = "You can hide this tab in the menu",
        [RG_LANG_ZH] = "您可以在菜单中隐藏此标签",
        [RG_LANG_KO] = "이 탭은 메뉴에서 숨길 수 있습니다",
    },
    {
        [RG_LANG_EN] = "You have no %s games",
        [RG_LANG_ZH] = "您没有%s游戏",
        [RG_LANG_KO] = "%s 게임이 없습니다",
    },
    {
        [RG_LANG_EN] = "File not found",
        [RG_LANG_ZH] = "文件未找到",
        [RG_LANG_KO] = "파일을 찾을 수 없습니다",
    },

    // rom options
    {
        [RG_LANG_EN] = "Folder",
        [RG_LANG_ZH] = "文件夹",
        [RG_LANG_KO] = "폴더",
    },
    {
        [RG_LANG_EN] = "Size",
        [RG_LANG_ZH] = "大小",
        [RG_LANG_KO] = "크기",
    },
    {
        [RG_LANG_EN] = "CRC32",
        [RG_LANG_ZH] = "CRC32",
        [RG_LANG_KO] = "CRC32",
    },
    {
        [RG_LANG_EN] = "Delete file",
        [RG_LANG_ZH] = "删除文件",
        [RG_LANG_KO] = "파일 삭제",
    },
    {
        [RG_LANG_EN] = "Close",
        [RG_LANG_ZH] = "关闭",
        [RG_LANG_KO] = "닫기",
    },
    {
        [RG_LANG_EN] = "File properties",
        [RG_LANG_ZH] = "文件属性",
        [RG_LANG_KO] = "파일 정보",
    },
    {
        [RG_LANG_EN] = "Delete selected file?",
        [RG_LANG_ZH] = "删除选中的文件?",
        [RG_LANG_KO] = "선택한 파일을 삭제할까요?",
    },

    // in-game menu
    {
        [RG_LANG_EN] = "Resume game",
        [RG_LANG_ZH] = "继续游戏",
        [RG_LANG_KO] = "이어하기",
    },
    {
        [RG_LANG_EN] = "New game",
        [RG_LANG_ZH] = "新游戏",
        [RG_LANG_KO] = "새 게임",
    },
    {
        [RG_LANG_EN] = "Del favorite",
        [RG_LANG_ZH] = "取消收藏",
        [RG_LANG_KO] = "즐겨찾기 삭제",
    },
    {
        [RG_LANG_EN] = "Add favorite",
        [RG_LANG_ZH] = "添加收藏",
        [RG_LANG_KO] = "즐겨찾기 추가",
    },
    {
        [RG_LANG_EN] = "Delete save",
        [RG_LANG_ZH] = "删除存档",
        [RG_LANG_KO] = "세이브 삭제",
    },
    {
        [RG_LANG_EN] = "Properties",
        [RG_LANG_ZH] = "属性",
        [RG_LANG_KO] = "정보",
    },
    {
        [RG_LANG_EN] = "Resume",
        [RG_LANG_ZH] = "继续",
        [RG_LANG_KO] = "이어하기",
    },
    {
        [RG_LANG_EN] = "Delete save?",
        [RG_LANG_ZH] = "删除存档?",
        [RG_LANG_KO] = "세이브를 삭제할까요?",
    },
    {
        [RG_LANG_EN] = "Delete sram file?",
        [RG_LANG_ZH] = "删除SRAM文件?",
        [RG_LANG_KO] = "SRAM 파일을 삭제할까요?",
    },
    // end of applications.c

    // rg_system.c
    {
        [RG_LANG_EN] = "App unresponsive... Hold MENU to quit!",
        [RG_LANG_ZH] = "应用无响应... 长按MENU退出!",
        [RG_LANG_KO] = "앱이 응답하지 않습니다... MENU를 길게 눌러 종료",
    },
    {
        [RG_LANG_EN] = "Reboot to factory ",
        [RG_LANG_ZH] = "重启到出厂",
        [RG_LANG_KO] = "공장 초기화로 재시작",
    },
    {
        [RG_LANG_EN] = "Reboot to launcher",
        [RG_LANG_ZH] = "重启到启动器",
        [RG_LANG_KO] = "런처로 재시작",
    },
    {
        [RG_LANG_EN] = "Recovery mode",
        [RG_LANG_ZH] = "恢复模式",
        [RG_LANG_KO] = "복구 모드",
    },
    {
        [RG_LANG_EN] = "System Panic!",
        [RG_LANG_ZH] = "系统崩溃!",
        [RG_LANG_KO] = "시스템 오류!",
    },
    {
        [RG_LANG_EN] = "Save failed",
        [RG_LANG_ZH] = "保存失败",
        [RG_LANG_KO] = "저장에 실패했습니다",
    },
    {
        [RG_LANG_EN] = "External memory not detected",
        [RG_LANG_ZH] = "未检测到外部内存",
        [RG_LANG_KO] = "외장 메모리를 찾을 수 없습니다",
    },
    {
        [RG_LANG_EN] = "Application crashed",
        [RG_LANG_ZH] = "应用程序崩溃",
        [RG_LANG_KO] = "앱이 비정상 종료되었습니다",
    },
    {
        [RG_LANG_EN] = "Log saved to SD Card.",
        [RG_LANG_ZH] = "日志已保存到SD卡。",
        [RG_LANG_KO] = "로그를 SD 카드에 저장했습니다.",
    },
    // gui.c
    {
        [RG_LANG_EN] = "Loading...",
        [RG_LANG_ZH] = "加载中...",
        [RG_LANG_KO] = "불러오는 중...",
    },
    {
        [RG_LANG_EN] = "List empty",
        [RG_LANG_ZH] = "列表为空",
        [RG_LANG_KO] = "목록이 비어 있습니다",
    },
    {
        [RG_LANG_EN] = "Bad cover",
        [RG_LANG_ZH] = "封面损坏",
        [RG_LANG_KO] = "손상된 커버",
    },
    {
        [RG_LANG_EN] = "No cover",
        [RG_LANG_ZH] = "无封面",
        [RG_LANG_KO] = "커버 없음",
    },
    // applications.c
    {
        [RG_LANG_EN] = "Compute",
        [RG_LANG_ZH] = "计算",
        [RG_LANG_KO] = "계산",
    },
    // rg_gui.c
    {
        [RG_LANG_EN] = "Theme",
        [RG_LANG_ZH] = "主题",
        [RG_LANG_KO] = "테마",
    },
    {
        [RG_LANG_EN] = "Default",
        [RG_LANG_ZH] = "默认",
        [RG_LANG_KO] = "기본값",
    },
    {
        [RG_LANG_EN] = "Credits",
        [RG_LANG_ZH] = "致谢",
        [RG_LANG_KO] = "만든 사람",
    },
    {
        [RG_LANG_EN] = "Manage networks",
        [RG_LANG_ZH] = "管理网络",
        [RG_LANG_KO] = "네트워크 관리",
    },
    {
        [RG_LANG_EN] = "Connect",
        [RG_LANG_ZH] = "连接",
        [RG_LANG_KO] = "연결",
    },
    {
        [RG_LANG_EN] = "Edit SSID",
        [RG_LANG_ZH] = "编辑SSID",
        [RG_LANG_KO] = "SSID 수정",
    },
    {
        [RG_LANG_EN] = "Edit Password",
        [RG_LANG_ZH] = "编辑密码",
        [RG_LANG_KO] = "비밀번호 수정",
    },
    {
        [RG_LANG_EN] = "Delete",
        [RG_LANG_ZH] = "删除",
        [RG_LANG_KO] = "삭제",
    },
    {
        [RG_LANG_EN] = "Enter new network name:",
        [RG_LANG_ZH] = "输入新网络名称:",
        [RG_LANG_KO] = "새 네트워크 이름을 입력하세요:",
    },
    {
        [RG_LANG_EN] = "Enter password (leave empty for open network):",
        [RG_LANG_ZH] = "输入密码(开放网络请留空):",
        [RG_LANG_KO] = "비밀번호를 입력하세요 (개방형이면 비워 두세요):",
    },
    {
        [RG_LANG_EN] = "Enter new password:",
        [RG_LANG_ZH] = "输入新密码:",
        [RG_LANG_KO] = "새 비밀번호를 입력하세요:",
    },
    {
        [RG_LANG_EN] = "Error",
        [RG_LANG_ZH] = "错误",
        [RG_LANG_KO] = "오류",
    },
    {
        [RG_LANG_EN] = "Failed to save network configuration",
        [RG_LANG_ZH] = "保存网络配置失败",
        [RG_LANG_KO] = "네트워크 설정을 저장하지 못했습니다",
    },
    {
        [RG_LANG_EN] = "Success",
        [RG_LANG_ZH] = "成功",
        [RG_LANG_KO] = "완료",
    },
    {
        [RG_LANG_EN] = "SSID updated",
        [RG_LANG_ZH] = "SSID已更新",
        [RG_LANG_KO] = "SSID를 변경했습니다",
    },
    {
        [RG_LANG_EN] = "Password updated",
        [RG_LANG_ZH] = "密码已更新",
        [RG_LANG_KO] = "비밀번호를 변경했습니다",
    },
    {
        [RG_LANG_EN] = "Delete Network",
        [RG_LANG_ZH] = "删除网络",
        [RG_LANG_KO] = "네트워크 삭제",
    },
    {
        [RG_LANG_EN] = "Are you sure you want to delete this network configuration?",
        [RG_LANG_ZH] = "确定要删除此网络配置吗?",
        [RG_LANG_KO] = "이 네트워크 설정을 삭제할까요?",
    },
    {
        [RG_LANG_EN] = "Network configuration deleted",
        [RG_LANG_ZH] = "网络配置已删除",
        [RG_LANG_KO] = "네트워크 설정을 삭제했습니다",
    },
    {
        [RG_LANG_EN] = "Cheats",
        [RG_LANG_ZH] = "作弊码",
        [RG_LANG_KO] = "치트",
    },
    {
        [RG_LANG_EN] = "Cheats not supported\nfor this emulator.",
        [RG_LANG_ZH] = "此模拟器不支持作弊码。",
        [RG_LANG_KO] = "이 에뮬레이터는\n치트를 지원하지 않습니다.",
    },
    {
        [RG_LANG_EN] = "No cheats available",
        [RG_LANG_ZH] = "无可用作弊码",
        [RG_LANG_KO] = "사용 가능한 치트가 없습니다",
    },
    {
        [RG_LANG_EN] = "Slot",
        [RG_LANG_ZH] = "槽位",
        [RG_LANG_KO] = "슬롯",
    },
    {
        [RG_LANG_EN] = "Border",
        [RG_LANG_ZH] = "边框",
        [RG_LANG_KO] = "테두리",
    },
    {
        [RG_LANG_EN] = "App unresponsive... Hold MENU to quit!",
        [RG_LANG_ZH] = "应用无响应... 按住MENU键退出!",
        [RG_LANG_KO] = "앱이 응답하지 않습니다... MENU를 길게 눌러 종료",
    },
    {
        [RG_LANG_EN] = "Application terminated!",
        [RG_LANG_ZH] = "应用程序已终止!",
        [RG_LANG_KO] = "앱을 강제 종료했습니다!",
    },
    {
        [RG_LANG_EN] = "Boot will continue but it will surely crash...",
        [RG_LANG_ZH] = "启动将继续但可能会崩溃...",
        [RG_LANG_KO] = "부팅은 계속되지만 거의 확실히 멈출 것입니다...",
    },
    // updater.c
    {
        [RG_LANG_EN] = "Connecting...",
        [RG_LANG_ZH] = "连接中...",
        [RG_LANG_KO] = "연결 중...",
    },
    {
        [RG_LANG_EN] = "Download failed!",
        [RG_LANG_ZH] = "下载失败!",
        [RG_LANG_KO] = "다운로드에 실패했습니다!",
    },
    {
        [RG_LANG_EN] = "Connection failed!",
        [RG_LANG_ZH] = "连接失败!",
        [RG_LANG_KO] = "연결에 실패했습니다!",
    },
    {
        [RG_LANG_EN] = "Out of memory!",
        [RG_LANG_ZH] = "内存不足!",
        [RG_LANG_KO] = "메모리가 부족합니다!",
    },
    {
        [RG_LANG_EN] = "File open failed!",
        [RG_LANG_ZH] = "文件打开失败!",
        [RG_LANG_KO] = "파일을 열지 못했습니다!",
    },
    {
        [RG_LANG_EN] = "Receiving file...",
        [RG_LANG_ZH] = "接收文件中...",
        [RG_LANG_KO] = "파일 받는 중...",
    },
    {
        [RG_LANG_EN] = "Received %d / %d",
        [RG_LANG_ZH] = "已接收 %d / %d",
        [RG_LANG_KO] = "받음 %d / %d",
    },
    {
        [RG_LANG_EN] = "Read/write error!",
        [RG_LANG_ZH] = "读写错误!",
        [RG_LANG_KO] = "읽기/쓰기 오류!",
    },
    {
        [RG_LANG_EN] = "Favorites",
        [RG_LANG_ZH] = "收藏夹",
        [RG_LANG_KO] = "즐겨찾기",
    },
    {
        [RG_LANG_EN] = "Recently played",
        [RG_LANG_ZH] = "最近游玩",
        [RG_LANG_KO] = "최근 플레이",
    },

    // Debug menu - rg_gui.c
    {
        [RG_LANG_EN] = "Debugging",
        [RG_LANG_ZH] = "调试",
        [RG_LANG_KO] = "디버깅",
    },
    {
        [RG_LANG_EN] = "Screen res",
        [RG_LANG_ZH] = "屏幕分辨率",
        [RG_LANG_KO] = "화면 해상도",
    },
    {
        [RG_LANG_EN] = "Source res",
        [RG_LANG_ZH] = "源分辨率",
        [RG_LANG_KO] = "원본 해상도",
    },
    {
        [RG_LANG_EN] = "Scaled res",
        [RG_LANG_ZH] = "缩放分辨率",
        [RG_LANG_KO] = "출력 해상도",
    },
    {
        [RG_LANG_EN] = "Stack HWM ",
        [RG_LANG_ZH] = "栈最低水位",
        [RG_LANG_KO] = "스택 최대",
    },
    {
        [RG_LANG_EN] = "Heap free ",
        [RG_LANG_ZH] = "堆空闲",
        [RG_LANG_KO] = "힙 여유 ",
    },
    {
        [RG_LANG_EN] = "Block free",
        [RG_LANG_ZH] = "块空闲",
        [RG_LANG_KO] = "블록 여유",
    },
    {
        [RG_LANG_EN] = "App name  ",
        [RG_LANG_ZH] = "应用名称",
        [RG_LANG_KO] = "앱 이름  ",
    },
    {
        [RG_LANG_EN] = "Network   ",
        [RG_LANG_ZH] = "网络",
        [RG_LANG_KO] = "네트워크 ",
    },
    {
        [RG_LANG_EN] = "Local time",
        [RG_LANG_ZH] = "本地时间",
        [RG_LANG_KO] = "현재 시각",
    },
    {
        [RG_LANG_EN] = "Timezone  ",
        [RG_LANG_ZH] = "时区",
        [RG_LANG_KO] = "표준시   ",
    },
    {
        [RG_LANG_EN] = "Uptime    ",
        [RG_LANG_ZH] = "运行时间",
        [RG_LANG_KO] = "가동 시간",
    },
    {
        [RG_LANG_EN] = "Battery   ",
        [RG_LANG_ZH] = "电池",
        [RG_LANG_KO] = "배터리   ",
    },
    {
        [RG_LANG_EN] = "Blit time ",
        [RG_LANG_ZH] = "绘制时间",
        [RG_LANG_KO] = "전송 시간",
    },
    {
        [RG_LANG_EN] = "Overclock",
        [RG_LANG_ZH] = "超频",
        [RG_LANG_KO] = "오버클럭",
    },
    {
        [RG_LANG_EN] = "Reboot to firmware",
        [RG_LANG_ZH] = "重启到固件",
        [RG_LANG_KO] = "펌웨어로 재시작",
    },
    {
        [RG_LANG_EN] = "Clear cache    ",
        [RG_LANG_ZH] = "清除缓存",
        [RG_LANG_KO] = "캐시 비우기",
    },
    {
        [RG_LANG_EN] = "Save screenshot",
        [RG_LANG_ZH] = "保存截图",
        [RG_LANG_KO] = "스크린샷 저장",
    },
    {
        [RG_LANG_EN] = "Save trace",
        [RG_LANG_ZH] = "保存追踪",
        [RG_LANG_KO] = "추적 기록 저장",
    },
    {
        [RG_LANG_EN] = "Cheats    ",
        [RG_LANG_ZH] = "作弊码",
        [RG_LANG_KO] = "치트     ",
    },
    {
        [RG_LANG_EN] = "Crash     ",
        [RG_LANG_ZH] = "崩溃测试",
        [RG_LANG_KO] = "강제 오류 ",
    },
    {
        [RG_LANG_EN] = "Crash test!",
        [RG_LANG_ZH] = "崩溃测试！",
        [RG_LANG_KO] = "강제 오류 테스트!",
    },
    {
        [RG_LANG_EN] = "Log level ",
        [RG_LANG_ZH] = "日志级别",
        [RG_LANG_KO] = "로그 수준",
    },

    // Network status
    {
        [RG_LANG_EN] = "not available",
        [RG_LANG_ZH] = "不可用",
        [RG_LANG_KO] = "사용할 수 없음",
    },
    {
        [RG_LANG_EN] = "disconnected",
        [RG_LANG_ZH] = "已断开",
        [RG_LANG_KO] = "연결 끊김",
    },

    // Save slot
    {
        [RG_LANG_EN] = "(last used)",
        [RG_LANG_ZH] = "(上次使用)",
        [RG_LANG_KO] = "(마지막 사용)",
    },
    {
        [RG_LANG_EN] = "is empty",
        [RG_LANG_ZH] = "为空",
        [RG_LANG_KO] = "비어 있음",
    },

    // Game menu title
    {
        [RG_LANG_EN] = "Retro-Go",
        [RG_LANG_ZH] = "游戏菜单",
        [RG_LANG_KO] = "Retro-Go",
    },

    // Virtual keyboard
    {
        [RG_LANG_EN] = "A=Type  B=Backspace  SELECT=%3s  START=OK  MENU/OPT=Cancel",
        [RG_LANG_ZH] = "A=输入 B=退格 SELECT=%3s START=确定 MENU/OPT=取消",
        [RG_LANG_KO] = "A=입력  B=지우기  SELECT=%3s  START=확인  MENU/OPT=취소",
    },

    // Browser file operations - browser.c
    {
        [RG_LANG_EN] = "Open file with...",
        [RG_LANG_ZH] = "打开方式...",
        [RG_LANG_KO] = "다음으로 열기...",
    },
    {
        [RG_LANG_EN] = "Rename file",
        [RG_LANG_ZH] = "重命名文件",
        [RG_LANG_KO] = "이름 바꾸기",
    },
    {
        [RG_LANG_EN] = "Copy file",
        [RG_LANG_ZH] = "复制文件",
        [RG_LANG_KO] = "파일 복사",
    },
    {
        [RG_LANG_EN] = "Move file",
        [RG_LANG_ZH] = "移动文件",
        [RG_LANG_KO] = "파일 이동",
    },

    // Lynx rotation - main_lynx.cpp
    {
        [RG_LANG_EN] = "Rotation",
        [RG_LANG_ZH] = "旋转",
        [RG_LANG_KO] = "회전",
    },
    {
        [RG_LANG_EN] = "Left",
        [RG_LANG_ZH] = "左",
        [RG_LANG_KO] = "왼쪽",
    },
    {
        [RG_LANG_EN] = "Right",
        [RG_LANG_ZH] = "右",
        [RG_LANG_KO] = "오른쪽",
    },

    // NES palette - main_nes.c
    {
        [RG_LANG_EN] = "Nofrendo",
        [RG_LANG_ZH] = "Nofrendo",
        [RG_LANG_KO] = "Nofrendo",
    },

    // Genesis options - gwenesis/main.c
    {
        [RG_LANG_EN] = "Render on core 1",
        [RG_LANG_ZH] = "核心1渲染",
        [RG_LANG_KO] = "코어 1에서 렌더링",
    },
    {
        [RG_LANG_EN] = "Sound on core 1",
        [RG_LANG_ZH] = "核心1音频",
        [RG_LANG_KO] = "코어 1에서 사운드",
    },

    // end of translations
};
