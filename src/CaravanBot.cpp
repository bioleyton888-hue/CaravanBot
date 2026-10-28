// CaravanBot - xNVSE plugin for Fallout: New Vegas (runtime 1.4.0.525).
//
// Plays the Caravan minigame for you:
//  - the toggle key (F8 by default, set in CaravanBot.ini next to this DLL) at the Caravan table
//    turns the bot on or off;
//  - on each of your turns the strategy engine (caravan_ai.h) picks a move, which is carried out
//    by moving the menu's own cursor and clicking its own Select / Discard buttons;
//  - while the bot is on, a valid deck is accepted automatically on the deck screen.
// With ExternalControl=1 in the INI it also lets an outside player drive the table: the table
// is written to <game dir>\CaravanBot\state.json (state.turn.awaiting marks your turn, with a
// turn id), and a move written to <game dir>\CaravanBot\move.json for that turn id is played.
// It patches no game code: everything runs on xNVSE's main-loop message.
//
// CaravanMenu layout comes from the type info in lStewieAl's BetterCaravan.pdb; the cursor
// behaviour from the disassembly of that plugin's hooks.

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "caravan_ai.h"

typedef unsigned int UInt32;
typedef UInt32 PluginHandle;

// ---- Minimal subset of xNVSE's PluginAPI.h ---------------------------------------------

struct PluginInfo
{
	enum { kInfoVersion = 1 };
	UInt32 infoVersion;
	const char *name;
	UInt32 version;
};

struct NVSEInterface
{
	UInt32 nvseVersion;
	UInt32 runtimeVersion;
	UInt32 editorVersion;
	UInt32 isEditor;
	void *RegisterCommand;
	void *SetOpcodeBase;
	void *(*QueryInterface)(UInt32 id);
	PluginHandle (*GetPluginHandle)(void);
	void *RegisterTypedCommand;
	const char *(*GetRuntimeDirectory)();
};

struct NVSEMessagingInterface
{
	struct Message
	{
		const char *sender;
		UInt32 type;
		UInt32 dataLen;
		void *data;
	};
	typedef void (*EventCallback)(Message *msg);

	UInt32 version;
	bool (*RegisterListener)(PluginHandle listener, const char *sender, EventCallback handler);
	bool (*Dispatch)(PluginHandle sender, UInt32 messageType, void *data, UInt32 dataLen, const char *receiver);
};

enum
{
	kInterface_Messaging = 2,
	kMessage_MainGameLoop = 20,
};

// ---- Game layout (offsets into CaravanMenu / TESCaravanCard) ----------------------------

enum : UInt32
{
	kAddr_CaravanMenuSingleton = 0x11D917C,

	kOff_CurrMenu = 0x28,
	kOff_GameStage = 0x2C,
	kOff_Tile_CDM_AllCardsButton = 0x5C,
	kOff_Tile_CDM_AddButton = 0x60,
	kOff_Tile_CDM_PlayButton = 0x68,
	kOff_Tile_CGM_SelectCardButton = 0x7C,
	kOff_Tile_CGM_DiscardCardButton = 0x80,
	kOff_Tile_CGM_DiscardTrackButton = 0x84,
	kOff_Difficulty = 0x108,
	kOff_CurrentAnte = 0xADC,
	kOff_OpponentFunds = 0xAE0,
	kOff_TotalPlayerFunds = 0xAE4,
	kOff_NumPlayerCardsInDeck = 0xAEC,
	kOff_TotalPlayerCards = 0xAF0,
	kOff_PlayerPileCards = 0xB04,
	kOff_OpponentPileCards = 0xB14,
	kOff_PlayerHandCards = 0xB24,
	kOff_OpponentHandCards = 0xB34,
	kOff_BoardCards = 0xB44,      // BSSimpleArray<TESCaravanCard*>, 6 tracks x 7 rows, track-major
	kOff_CaravanValues = 0xDE4,   // CaravanValue[6] {direction, suit, pileValue}
	kOff_SelectedCardIdx = 0xE2C,
	kOff_SelectedTrack = 0xE30,
	kOff_SelectedRow = 0xE34,
	kOff_Flags = 0xE74,
	kOff_LastPressedArrowKey = 0xE80,

	kOff_FormRefID = 0x0C,
	kOff_CardValue = 0xB4,
	kOff_CardSuit = 0xB8,

	kVtbl_HandleClick = 3,

	// bool CaravanMenu::CanPlaceCard(int track, int row, int handCardIdx, bool): the engine's
	// placement rule check (Better Caravan loops it over the board to grey out unusable cards).
	kAddr_CaravanMenu_CanPlaceCard = 0x74F6C0,

	// Keyboard, messages and sounds (as Better Caravan, JIP and ShowOff call them).
	kAddr_OSInputGlobalsSingleton = 0x11F35CC,
	kAddr_OSInputGlobals_GetKeyState = 0xA24180,  // bool (int DirectInput key, KeyState)
	kAddr_QueueUIMessage = 0x7052F0,              // cdecl: corner message
	kAddr_BSWin32AudioSingleton = 0x11F6D98,
	kAddr_BSWin32Audio_GetSound = 0xAD7550,       // (Sound *out, const char *editorID, UInt32 flags)
	kAddr_Sound_Play = 0xAD8830,
};

enum
{
	kMenu_Bet = 0,
	kMenu_BuildDeck = 1,
	kMenu_Game = 2,
	kMenu_Results = 3,

	kFlag_PreventInputs = 0x1,      // set for the whole NPC turn and during every animation
	kFlag_NotEnoughCardsInDeck = 0x2,
	kFlag_RestrictToFirstColumn = 0x100,
	kFlag_PlayerWon = 0x400,           // on the results screen
	kFlag_RestrictToFirstRow = 0x800,  // with RestrictToFirstColumn: the opening (one number card per caravan)
	kFlag_IsPlaceCard = 0x1000,
	kFlag_IsDiscardTrack = 0x2000,
	kFlag_AllowInputs = 0x10000,

	kButton_CDM_AllCards = 0xB,
	kButton_CDM_Add = 0xC,
	kButton_CDM_Play = 0xE,
	kButton_CGM_SelectCard = 19,
	kButton_CGM_DiscardCard = 20,
	kButton_CGM_DiscardTrack = 21,

	// Values the engine reads from lastPressedArrowKey (as Better Caravan's WASD support writes them).
	kArrow_Left = 0x80000001,
	kArrow_Right = 0x80000002,
	kArrow_Up = 0x80000003,
	kArrow_Down = 0x80000004,

	kMinDeckSize = 30,

	kKeyState_Held = 0,
	kDIK_F8 = 0x42,
};

enum
{
	kNumRows = 7,
	kNumTracks = 6,
	kMaxHand = 16,
	kMaxCell = 8,
	kMaxArray = 64,  // anything bigger means we are reading the wrong memory
};

struct SimpleArray  // BSSimpleArray<T*>
{
	void *vtbl;
	void **data;
	UInt32 size;
	UInt32 alloc;
};

struct Card
{
	int value;
	int suit;
	UInt32 formID;
};

struct CaravanValue
{
	int direction;
	int suit;
	int pileValue;
};

struct Snapshot
{
	bool active;
	bool ok;
	UInt32 menu, stage, flags, difficulty;
	UInt32 ante, playerFunds, opponentFunds;
	UInt32 playerDeckCount, opponentDeckCount;
	UInt32 cardsInDeck, totalCards;
	int numPlayerHand;
	Card playerHand[kMaxHand];
	int numOpponentHand;
	int cellCount[kNumRows][kNumTracks];
	Card cells[kNumRows][kNumTracks][kMaxCell];
	CaravanValue caravanValues[kNumTracks];
	int selectedCardIdx, selectedTrack, selectedRow;
};

// ---- Reading and poking the menu (no C++ objects in here: __try needs a plain frame) -----

template <typename T>
static T Read(UInt32 base, UInt32 offset) { return *(T *)(base + offset); }

static UInt32 GetMenu() { return *(UInt32 *)kAddr_CaravanMenuSingleton; }

static bool ReadCard(void *ptr, Card &out)
{
	if (!ptr)
		return false;
	UInt32 card = (UInt32)ptr;
	out.value = Read<int>(card, kOff_CardValue);
	out.suit = Read<int>(card, kOff_CardSuit);
	out.formID = Read<UInt32>(card, kOff_FormRefID);
	return true;
}

static int ReadCardArray(UInt32 arrayAddr, Card *out, int maxOut)
{
	SimpleArray *arr = (SimpleArray *)arrayAddr;
	if (arr->size > kMaxArray)
		return -1;
	int n = 0;
	for (UInt32 i = 0; i < arr->size && n < maxOut; i++)
		if (ReadCard(arr->data[i], out[n]))
			n++;
	return n;
}

static bool TakeSnapshot(Snapshot &s)
{
	memset(&s, 0, sizeof(s));
	__try
	{
		UInt32 menu = GetMenu();
		if (!menu)
			return true;
		s.active = true;
		s.menu = Read<UInt32>(menu, kOff_CurrMenu);
		s.stage = Read<UInt32>(menu, kOff_GameStage);
		s.flags = Read<UInt32>(menu, kOff_Flags);
		s.difficulty = Read<UInt32>(menu, kOff_Difficulty);
		s.ante = Read<UInt32>(menu, kOff_CurrentAnte);
		s.playerFunds = Read<UInt32>(menu, kOff_TotalPlayerFunds);
		s.opponentFunds = Read<UInt32>(menu, kOff_OpponentFunds);
		s.cardsInDeck = Read<UInt32>(menu, kOff_NumPlayerCardsInDeck);
		s.totalCards = Read<UInt32>(menu, kOff_TotalPlayerCards);
		s.playerDeckCount = ((SimpleArray *)(menu + kOff_PlayerPileCards))->size;
		s.opponentDeckCount = ((SimpleArray *)(menu + kOff_OpponentPileCards))->size;

		s.numPlayerHand = ReadCardArray(menu + kOff_PlayerHandCards, s.playerHand, kMaxHand);
		// Only the opponent's hand size: reading their cards would be cheating.
		s.numOpponentHand = (int)((SimpleArray *)(menu + kOff_OpponentHandCards))->size;
		if (s.numPlayerHand < 0)
			return false;

		for (int r = 0; r < kNumRows; r++)
			for (int t = 0; t < kNumTracks; t++)
			{
				// Track-major: the PDB prints the dimensions as [7][6], but the cards sit at track * 7 + row
				// (confirmed in game: a card on track 4 row 1 shows up at index 29).
				UInt32 cell = menu + kOff_BoardCards + (t * kNumRows + r) * sizeof(SimpleArray);
				s.cellCount[r][t] = ReadCardArray(cell, s.cells[r][t], kMaxCell);
				if (s.cellCount[r][t] < 0)
					return false;
			}

		for (int t = 0; t < kNumTracks; t++)
			s.caravanValues[t] = Read<CaravanValue>(menu, kOff_CaravanValues + t * sizeof(CaravanValue));

		s.selectedCardIdx = Read<int>(menu, kOff_SelectedCardIdx);
		s.selectedTrack = Read<int>(menu, kOff_SelectedTrack);
		s.selectedRow = Read<int>(menu, kOff_SelectedRow);
		s.ok = true;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

typedef void(__fastcall *HandleClick_t)(UInt32 menu, void *edx, int tileID, UInt32 tile);

static bool ClickButton(int buttonID, UInt32 tileOffset)
{
	__try
	{
		UInt32 menu = GetMenu();
		if (!menu)
			return false;
		UInt32 *vtbl = *(UInt32 **)menu;
		((HandleClick_t)vtbl[kVtbl_HandleClick])(menu, nullptr, buttonID, Read<UInt32>(menu, tileOffset));
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

typedef bool(__fastcall *CanPlaceCard_t)(UInt32 menu, void *edx, int track, int row, int handCardIdx, bool unk);

// legal[i][t][r] = the engine allows hand card i at track t, row r. CanPlaceCard applies the
// normal placement rules but not the opening restriction (the game enforces that through the
// RestrictToFirstColumn/Row flags, which lock the cursor to the first empty caravan), so during
// the opening only that slot is kept: a number card on your first empty caravan, row 0.
static bool ScanLegalPlacements(const Snapshot &s, bool legal[kMaxHand][kNumTracks][kNumRows])
{
	memset(legal, 0, sizeof(bool) * kMaxHand * kNumTracks * kNumRows);
	__try
	{
		UInt32 menu = GetMenu();
		if (!menu)
			return false;
		auto canPlace = (CanPlaceCard_t)kAddr_CaravanMenu_CanPlaceCard;
		for (int i = 0; i < s.numPlayerHand && i < kMaxHand; i++)
			for (int t = 0; t < kNumTracks; t++)
				for (int r = 0; r < kNumRows; r++)
					legal[i][t][r] = canPlace(menu, nullptr, t, r, i, false);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}

	bool opening = (s.flags & (kFlag_RestrictToFirstColumn | kFlag_RestrictToFirstRow)) != 0;
	if (opening)
	{
		int firstEmpty = 0;
		while (firstEmpty < 3 && s.cellCount[0][firstEmpty] > 0)
			firstEmpty++;
		for (int i = 0; i < s.numPlayerHand && i < kMaxHand; i++)
		{
			const Card &c = s.playerHand[i];
			bool isNumber = c.value >= 1 && c.value <= 10 && c.suit != 5;
			for (int t = 0; t < kNumTracks; t++)
				for (int r = 0; r < kNumRows; r++)
					legal[i][t][r] = legal[i][t][r] && isNumber && t == firstEmpty && r == 0;
		}
	}
	return true;
}

static bool PressArrow(UInt32 arrow)
{
	__try
	{
		UInt32 menu = GetMenu();
		if (!menu)
			return false;
		*(UInt32 *)(menu + kOff_LastPressedArrowKey) = arrow;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

typedef bool(__fastcall *GetKeyState_t)(UInt32 input, void *edx, int key, int state);

static bool IsKeyHeld(int key)
{
	__try
	{
		UInt32 input = *(UInt32 *)kAddr_OSInputGlobalsSingleton;
		return input && ((GetKeyState_t)kAddr_OSInputGlobals_GetKeyState)(input, nullptr, key, kKeyState_Held);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

typedef bool(__cdecl *QueueUIMessage_t)(const char *msg, UInt32 emotion, const char *ddsPath, const char *soundName, float seconds, bool next);

static void ShowGameMessage(const char *msg)
{
	__try
	{
		((QueueUIMessage_t)kAddr_QueueUIMessage)(msg, 0, nullptr, nullptr, 2.5f, false);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}
}

struct Sound  // BSWin32Audio sound handle
{
	UInt32 soundKey = 0xFFFFFFFF;
	UInt32 unk04 = 0;
	UInt32 unk08 = 0;
	UInt32 pad = 0;
};

typedef void(__fastcall *GetSound_t)(UInt32 audio, void *edx, Sound *out, const char *editorID, UInt32 flags);
typedef void(__fastcall *SoundPlay_t)(Sound *sound, void *edx, int unk);

static void PlayGameSound(const char *editorID)
{
	__try
	{
		UInt32 audio = *(UInt32 *)kAddr_BSWin32AudioSingleton;
		if (!audio)
			return;
		Sound sound;
		((GetSound_t)kAddr_BSWin32Audio_GetSound)(audio, nullptr, &sound, editorID, 0x121);
		((SoundPlay_t)kAddr_Sound_Play)(&sound, nullptr, 0);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}
}

// ---- Small helpers ----------------------------------------------------------------------

static const char *MenuName(UInt32 menu)
{
	switch (menu)
	{
	case kMenu_Bet: return "bet";
	case kMenu_BuildDeck: return "build_deck";
	case kMenu_Game: return "game";
	case kMenu_Results: return "results";
	default: return "unknown";
	}
}

static std::string CardCode(const Card &c)
{
	static const char *kValues[] = {"?", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "?", "J", "Q", "K", "JOKER"};
	static const char kSuits[] = {'?', 'H', 'S', 'D', 'C', '*'};
	if (c.value == 15 || c.suit == 5)
		return "JOKER";
	std::string code = (c.value >= 0 && c.value <= 15) ? kValues[c.value] : "?";
	code += (c.suit >= 0 && c.suit <= 5) ? kSuits[c.suit] : '?';
	return code;
}

// The opening: one number card per caravan, no discards.
static bool IsOpening(const Snapshot &s)
{
	const UInt32 both = kFlag_RestrictToFirstColumn | kFlag_RestrictToFirstRow;
	return (s.flags & both) == both;
}

// Signature of what changes when someone plays: hands, board and caravan values (not the cursor).
static std::string TableSignature(const Snapshot &s)
{
	std::string sig;
	for (int i = 0; i < s.numPlayerHand; i++)
		sig += CardCode(s.playerHand[i]) + ",";
	char buf[32];
	sprintf_s(buf, "|%d|%u|%u|", s.numOpponentHand, s.playerDeckCount, s.opponentDeckCount);
	sig += buf;
	for (int r = 0; r < kNumRows; r++)
		for (int t = 0; t < kNumTracks; t++)
			for (int i = 0; i < s.cellCount[r][t]; i++)
				sig += CardCode(s.cells[r][t][i]) + (char)('a' + r * kNumTracks + t);
	return sig;
}

static void JsonEscapeInto(std::string &j, const std::string &text)
{
	for (char c : text)
	{
		if (c == '"' || c == '\\')
			j += '\\';
		if ((unsigned char)c >= 0x20)
			j += c;
	}
}

static int JsonInt(const std::string &s, const char *key, int def)
{
	std::string k = std::string("\"") + key + "\"";
	size_t p = s.find(k);
	if (p == std::string::npos)
		return def;
	p = s.find(':', p + k.size());
	if (p == std::string::npos)
		return def;
	char *end = nullptr;
	long v = strtol(s.c_str() + p + 1, &end, 10);
	return end == s.c_str() + p + 1 ? def : (int)v;
}

static std::string JsonStr(const std::string &s, const char *key)
{
	std::string k = std::string("\"") + key + "\"";
	size_t p = s.find(k);
	if (p == std::string::npos)
		return "";
	p = s.find('"', s.find(':', p + k.size()));
	if (p == std::string::npos)
		return "";
	size_t e = s.find('"', p + 1);
	return e == std::string::npos ? "" : s.substr(p + 1, e - p - 1);
}

// Board positions as seen on screen. Player tracks 0,1,2 sit left to right below the middle,
// NPC tracks 5,4,3 left to right above it; rows grow away from the middle.
// col: 0..2 left to right. y: player row r -> r, NPC row r -> -(r + 1) (so "up" lowers y).
static int TrackCol(int track) { return track < 3 ? track : 5 - track; }
static bool IsNpcTrack(int track) { return track >= 3; }
static int ScreenY(int track, int row) { return IsNpcTrack(track) ? -(row + 1) : row; }

// ---- Plugin state -----------------------------------------------------------------------

static PluginHandle g_pluginHandle = 0;
static std::string g_gameDir;  // no trailing backslash
static std::string g_outDir;
static std::string g_iniPath;  // CaravanBot.ini next to the DLL (Data\NVSE\Plugins)
static bool g_external = false;  // ExternalControl=1: write state.json and accept move.json
static std::string g_lastJson;
static UInt32 g_frame = 0;
static UInt32 g_seq = 0;

struct Move
{
	int turn;
	std::string action;  // "place", "discard", "discard_track"
	int card;
	int track;
	int row;             // -1: any row on that track
};

enum Phase
{
	kPhase_Idle,
	kPhase_SelectHandCard,
	kPhase_WaitForMode,
	kPhase_Navigate,
};

static struct
{
	UInt32 id = 0;                // increments each time it becomes the player's turn (never resets)
	UInt32 match = 0;             // increments each time a game starts
	int number = 0;               // the player's turn number within the current match
	UInt32 lastMenu = (UInt32)-1;
	bool awaiting = false;
	std::string stableSig;        // table signature while it is the player's turn, for a short debounce
	DWORD stableSince = 0;
	std::string lastMoveSig;      // table signature when the last move was carried out
	DWORD lastMoveTick = 0;
	std::string lastResult = "none";
	bool legalValid = false;      // legal[] was scanned for the current awaited turn
	bool legal[kMaxHand][kNumTracks][kNumRows];
} g_turn;

static struct
{
	Phase phase = kPhase_Idle;
	Move move;
	DWORD nextActionTick = 0;
	DWORD deadline = 0;
	int presses = 0;
	int stuck = 0;
	int retries = 0;              // extra clicks when the game ignored the first one
	int lastIdx = -99, lastTrack = -99, lastRow = -99;
} g_exec;

static struct
{
	bool enabled = false;
	int key = kDIK_F8;          // DirectInput key code, from ToggleKey in CaravanBot.ini
	bool keyWasDown = false;
	bool hintShown = false;     // the "press F8" hint is shown once per game session
	UInt32 lastTurnId = 0;      // the bot plays each awaited turn once
} g_bot;

static bool g_deckPlayClicked = false;
static DWORD g_deckEnteredTick = 0;

static struct DeckFill
{
	UInt32 lastCount = (UInt32)-1;  // -1 until the first attempt
	int noProgress = 0;
	DWORD nextActionTick = 0;
	bool gaveUp = false;
} g_deck;

static void Log(const std::string &msg)
{
	FILE *f = nullptr;
	if (fopen_s(&f, (g_outDir + "\\CaravanBot.log").c_str(), "a") == 0 && f)
	{
		SYSTEMTIME t;
		GetLocalTime(&t);
		fprintf(f, "%02d:%02d:%02d %s\n", t.wHour, t.wMinute, t.wSecond, msg.c_str());
		fclose(f);
	}
}

// ---- JSON -------------------------------------------------------------------------------

static std::string CardJson(const Card &c)
{
	char buf[96];
	sprintf_s(buf, "{\"card\":\"%s\",\"value\":%d,\"suit\":%d,\"formID\":\"%08X\"}",
		CardCode(c).c_str(), c.value, c.suit, c.formID);
	return buf;
}

static std::string SnapshotJson(const Snapshot &s)
{
	if (!s.active)
		return "{\"active\":false}";
	if (!s.ok)
		return "{\"active\":true,\"error\":\"could not read the Caravan menu\"}";

	char buf[256];
	std::string j = "{\"active\":true";
	sprintf_s(buf, ",\"menu\":\"%s\",\"stage\":%u,\"flags\":\"0x%X\",\"difficulty\":%u", MenuName(s.menu), s.stage, s.flags, s.difficulty);
	j += buf;
	sprintf_s(buf, ",\"ante\":%u,\"playerFunds\":%u,\"opponentFunds\":%u", s.ante, s.playerFunds, s.opponentFunds);
	j += buf;
	sprintf_s(buf, ",\"cardsInDeck\":%u,\"totalCards\":%u", s.cardsInDeck, s.totalCards);
	j += buf;
	sprintf_s(buf, ",\"playerDeckCount\":%u,\"opponentDeckCount\":%u,\"opponentHandCount\":%d", s.playerDeckCount, s.opponentDeckCount, s.numOpponentHand);
	j += buf;

	j += ",\"playerHand\":[";
	for (int i = 0; i < s.numPlayerHand; i++)
		j += (i ? "," : "") + CardJson(s.playerHand[i]);
	j += "]";

	// tracks[t].rows = {row, cards}: the cards stacked at that position (number card + attached face cards).
	j += ",\"tracks\":[";
	for (int t = 0; t < kNumTracks; t++)
	{
		const CaravanValue &v = s.caravanValues[t];
		sprintf_s(buf, "%s{\"index\":%d,\"owner\":\"%s\",\"col\":%d,\"value\":%d,\"direction\":%d,\"suit\":%d,\"rows\":[",
			t ? "," : "", t, IsNpcTrack(t) ? "npc" : "player", TrackCol(t), v.pileValue, v.direction, v.suit);
		j += buf;
		bool firstRow = true;
		for (int r = 0; r < kNumRows; r++)
		{
			if (s.cellCount[r][t] == 0)
				continue;
			sprintf_s(buf, "%s{\"row\":%d,\"cards\":[", firstRow ? "" : ",", r);
			j += buf;
			firstRow = false;
			for (int i = 0; i < s.cellCount[r][t]; i++)
				j += (i ? "," : "") + CardJson(s.cells[r][t][i]);
			j += "]}";
		}
		j += "]}";
	}
	j += "]";

	sprintf_s(buf, ",\"selected\":{\"cardIdx\":%d,\"track\":%d,\"row\":%d}", s.selectedCardIdx, s.selectedTrack, s.selectedRow);
	j += buf;
	bool opening = IsOpening(s);
	sprintf_s(buf, ",\"turn\":{\"id\":%u,\"match\":%u,\"number\":%d,\"awaiting\":%s,\"executing\":%s,\"opening\":%s,\"lastResult\":\"",
		g_turn.id, g_turn.match, g_turn.number, g_turn.awaiting ? "true" : "false",
		g_exec.phase != kPhase_Idle ? "true" : "false", opening ? "true" : "false");
	j += buf;
	JsonEscapeInto(j, g_turn.lastResult);
	j += "\"";

	// Legal moves for the awaited turn, straight from the engine's CanPlaceCard.
	if (g_turn.awaiting && g_turn.legalValid)
	{
		j += ",\"legalMoves\":[";
		bool firstCard = true;
		for (int i = 0; i < s.numPlayerHand; i++)
		{
			std::string targets;
			for (int t = 0; t < kNumTracks; t++)
				for (int r = 0; r < kNumRows; r++)
					if (g_turn.legal[i][t][r])
					{
						sprintf_s(buf, "%s{\"track\":%d,\"row\":%d}", targets.empty() ? "" : ",", t, r);
						targets += buf;
					}
			if (targets.empty())
				continue;
			sprintf_s(buf, "%s{\"card\":%d,\"code\":\"%s\",\"targets\":[", firstCard ? "" : ",", i, CardCode(s.playerHand[i]).c_str());
			j += buf + targets + "]}";
			firstCard = false;
		}
		j += "]";
		// Discarding a card or one of your caravans is not allowed during the opening.
		j += ",\"discardTracks\":[";
		bool firstTrack = true;
		for (int t = 0; t < 3 && !opening; t++)
		{
			bool hasCards = false;
			for (int r = 0; r < kNumRows; r++)
				hasCards |= s.cellCount[r][t] > 0;
			if (hasCards)
			{
				j += (firstTrack ? "" : ",") + std::to_string(t);
				firstTrack = false;
			}
		}
		j += std::string("],\"canDiscardCard\":") + (opening ? "false" : "true");
	}
	j += "}}";
	return j;
}

static void WriteState(const std::string &json)
{
	std::string tmp = g_outDir + "\\state.json.tmp";
	std::string dest = g_outDir + "\\state.json";
	FILE *f = nullptr;
	if (fopen_s(&f, tmp.c_str(), "wb") != 0 || !f)
		return;
	char header[64];
	sprintf_s(header, "{\"seq\":%u,\"state\":", ++g_seq);
	fputs(header, f);
	fputs(json.c_str(), f);
	fputs("}\n", f);
	fclose(f);
	MoveFileExA(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING);
}

// ---- Turn detection ---------------------------------------------------------------------

// It is the player's turn when the game screen is idle (stage 0) and the engine accepts input:
// it sets PreventInputs for the whole NPC turn and every animation, and AllowInputs otherwise.
static bool IsPlayerTurn(const Snapshot &s)
{
	return s.ok && s.menu == kMenu_Game && s.stage == 0 && s.numPlayerHand > 0 &&
		(s.flags & (kFlag_PreventInputs | kFlag_AllowInputs)) == kFlag_AllowInputs &&
		!(s.flags & (kFlag_IsPlaceCard | kFlag_IsDiscardTrack));
}

static void UpdateTurn(const Snapshot &s, DWORD now)
{
	UInt32 menu = s.ok ? s.menu : (UInt32)-1;
	if (menu == kMenu_Game && g_turn.lastMenu != kMenu_Game)
	{
		g_turn.match++;
		g_turn.number = 0;
		g_turn.lastMoveSig.clear();
		Log("Match " + std::to_string(g_turn.match) + " started");
	}
	if (menu == kMenu_Results && g_turn.lastMenu != kMenu_Results)
	{
		const CaravanValue *v = s.caravanValues;
		char buf[128];
		sprintf_s(buf, "Result: %s | mine %d/%d/%d vs npc %d/%d/%d", (s.flags & kFlag_PlayerWon) ? "WON" : "LOST",
			v[0].pileValue, v[1].pileValue, v[2].pileValue, v[5].pileValue, v[4].pileValue, v[3].pileValue);
		Log(buf);
	}
	g_turn.lastMenu = menu;

	bool playerTurn = IsPlayerTurn(s);
	std::string sig = playerTurn ? TableSignature(s) : "";
	if (sig != g_turn.stableSig)
	{
		g_turn.stableSig = sig;
		g_turn.stableSince = now;
	}
	if (g_turn.awaiting)
	{
		if (!playerTurn)
			g_turn.awaiting = false;
		return;
	}
	// A short debounce so a card that is still settling is not read mid-change.
	if (!playerTurn || g_exec.phase != kPhase_Idle || now - g_turn.stableSince < 250)
		return;
	// After a move, wait until the table has changed; if it never does, offer the turn again.
	if (sig == g_turn.lastMoveSig && now - g_turn.lastMoveTick < 15000)
		return;
	if (sig == g_turn.lastMoveSig)
		g_turn.lastResult = "the last move did not change the table";
	g_turn.legalValid = ScanLegalPlacements(s, g_turn.legal);
	g_turn.awaiting = true;
	g_turn.id++;
	g_turn.number++;
	Log("Awaiting move for turn " + std::to_string(g_turn.id) + " (match " + std::to_string(g_turn.match) +
		", turn " + std::to_string(g_turn.number) + (g_turn.legalValid ? "" : ", legal-move scan failed") + ")");
}

// ---- Move execution ---------------------------------------------------------------------

static void FinishMove(const Snapshot &s, const std::string &result)
{
	g_exec.phase = kPhase_Idle;
	g_turn.lastResult = result;
	g_turn.lastMoveSig = TableSignature(s);
	g_turn.lastMoveTick = GetTickCount();
	Log("Turn " + std::to_string(g_exec.move.turn) + ": " + result);
}

// Checks the move against the engine's rules and starts carrying it out. A rejected move keeps
// the turn open (same id), so a corrected move can be sent straight away.
static bool StartMove(Move m, DWORD now)
{
	std::string rejection;
	if (m.action == "place")
	{
		if (m.card < 0 || m.card >= kMaxHand || m.track < 0 || m.track >= kNumTracks || m.row >= kNumRows)
			rejection = "card, track or row out of range";
		else if (g_turn.legalValid && m.row >= 0 && !g_turn.legal[m.card][m.track][m.row])
			rejection = "the game does not allow that card there (see legalMoves)";
		else if (g_turn.legalValid && m.row < 0)
		{
			// "Any row": use the first row the engine allows on that track.
			for (int r = 0; r < kNumRows && m.row < 0; r++)
				if (g_turn.legal[m.card][m.track][r])
					m.row = r;
			if (m.row < 0)
				rejection = "that card cannot go anywhere on that track (see legalMoves)";
		}
	}
	else if (m.action == "discard_track")
	{
		if (m.track < 0 || m.track > 2)
			rejection = "discard_track needs one of your tracks (0-2)";
	}
	else if (m.action != "discard")
	{
		rejection = "unknown action '" + m.action + "'";
	}
	if (!rejection.empty())
	{
		g_turn.lastResult = "rejected: " + rejection;
		Log("Turn " + std::to_string(m.turn) + ": " + g_turn.lastResult);
		return false;
	}

	g_exec.move = m;
	g_exec.presses = g_exec.stuck = g_exec.retries = 0;
	g_exec.lastIdx = g_exec.lastTrack = g_exec.lastRow = -99;
	g_exec.nextActionTick = now;
	g_turn.awaiting = false;
	Log("Turn " + std::to_string(m.turn) + ": got move " + m.action + " card=" + std::to_string(m.card) +
		" track=" + std::to_string(m.track) + " row=" + std::to_string(m.row));

	if (m.action == "discard_track")
	{
		ClickButton(kButton_CGM_DiscardTrack, kOff_Tile_CGM_DiscardTrackButton);
		g_exec.phase = kPhase_WaitForMode;
		g_exec.deadline = now + 2000;
		g_exec.nextActionTick = now + 200;
	}
	else
	{
		g_exec.phase = kPhase_SelectHandCard;
	}
	return true;
}

// ExternalControl: play the move an outside player wrote to move.json for the awaited turn.
static void TryExternalMove(DWORD now)
{
	if (!g_turn.awaiting || g_exec.phase != kPhase_Idle)
		return;
	std::string path = g_outDir + "\\move.json";
	FILE *f = nullptr;
	if (fopen_s(&f, path.c_str(), "rb") != 0 || !f)
		return;
	char buf[1024] = {};
	fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	std::string text = buf;

	Move m;
	m.turn = JsonInt(text, "turn", -1);
	if (m.turn != (int)g_turn.id)
		return;  // stale or early move: leave the file, a newer one will replace it
	m.action = JsonStr(text, "action");
	m.card = JsonInt(text, "card", -1);
	m.track = JsonInt(text, "track", -1);
	m.row = JsonInt(text, "row", -1);
	MoveFileExA(path.c_str(), (g_outDir + "\\move.done.json").c_str(), MOVEFILE_REPLACE_EXISTING);
	StartMove(m, now);
}

// The bot: on each awaited turn, ask the strategy engine for a move and play it.
static void BotPlayTurn(const Snapshot &s, DWORD now)
{
	if (!g_turn.awaiting || g_exec.phase != kPhase_Idle || g_bot.lastTurnId == g_turn.id)
		return;
	g_bot.lastTurnId = g_turn.id;
	if (!g_turn.legalValid)
	{
		Log("Turn " + std::to_string(g_turn.id) + ": could not read the legal moves; play this turn yourself");
		return;
	}

	caravan_ai::Input in = {};
	for (int t = 0; t < kNumTracks; t++)
		for (int r = 0; r < kNumRows; r++)
		{
			if (s.cellCount[r][t] == 0)
				continue;
			int kings = 0;
			for (int i = 1; i < s.cellCount[r][t]; i++)
				kings += s.cells[r][t][i].value == caravan_ai::kKing;
			in.board.rows[t][in.board.count[t]++] = {s.cells[r][t][0].value, s.cells[r][t][0].suit, kings};
		}
	for (int i = 0; i < s.numPlayerHand; i++)
		in.hand.push_back({s.playerHand[i].value, s.playerHand[i].suit, CardCode(s.playerHand[i])});
	for (int i = 0; i < s.numPlayerHand && i < kMaxHand; i++)
	{
		caravan_ai::LegalMove lm = {i, {}};
		for (int t = 0; t < kNumTracks; t++)
			for (int r = 0; r < kNumRows; r++)
				if (g_turn.legal[i][t][r])
					lm.targets.push_back({t, r});
		if (!lm.targets.empty())
			in.legal.push_back(lm);
	}
	bool opening = IsOpening(s);
	in.canDiscardCard = !opening;
	for (int t = 0; t < 3 && !opening; t++)
		if (in.board.count[t] > 0)
			in.discardTracks.push_back(t);
	in.opponentHand = s.numOpponentHand;

	caravan_ai::Choice c = caravan_ai::ChooseMove(in);
	std::string hand;
	for (auto &h : in.hand)
		hand += (hand.empty() ? "" : " ") + h.code;
	Log("Turn " + std::to_string(g_turn.id) + " (#" + std::to_string(g_turn.number) + ") hand [" + hand + "]: " + c.reason);
	if (c.action == caravan_ai::kAction_None)
		return;

	Move m;
	m.turn = (int)g_turn.id;
	m.action = c.action == caravan_ai::kAction_Place ? "place" : c.action == caravan_ai::kAction_Discard ? "discard" : "discard_track";
	m.card = c.card;
	m.track = c.track;
	m.row = c.row;
	StartMove(m, now);
}

static void StepMove(const Snapshot &s, DWORD now)
{
	if (g_exec.phase == kPhase_Idle || now < g_exec.nextActionTick)
		return;
	const Move &m = g_exec.move;
	if (!s.ok || s.menu != kMenu_Game)
	{
		FinishMove(s, "error: left the game screen during the move");
		return;
	}

	switch (g_exec.phase)
	{
	case kPhase_SelectHandCard:
	{
		if (m.card < 0 || m.card >= s.numPlayerHand)
		{
			FinishMove(s, "error: card index out of range");
			return;
		}
		// A late answer to an earlier Select click: the card is already picked up, so clicking
		// Select again would drop it wherever the cursor is.
		if (m.action == "place" && (s.flags & kFlag_IsPlaceCard))
		{
			g_exec.phase = kPhase_Navigate;
			g_exec.presses = g_exec.stuck = 0;
			g_exec.nextActionTick = now + 150;
			return;
		}
		if (s.selectedCardIdx == m.card)
		{
			if (m.action == "discard")
			{
				ClickButton(kButton_CGM_DiscardCard, kOff_Tile_CGM_DiscardCardButton);
				FinishMove(s, "ok: discarded " + CardCode(s.playerHand[m.card]));
				return;
			}
			ClickButton(kButton_CGM_SelectCard, kOff_Tile_CGM_SelectCardButton);
			g_exec.phase = kPhase_WaitForMode;
			g_exec.deadline = now + 2000;
			g_exec.presses = g_exec.stuck = 0;
			g_exec.nextActionTick = now + 200;
			return;
		}
		g_exec.stuck = s.selectedCardIdx == g_exec.lastIdx ? g_exec.stuck + 1 : 0;
		if (g_exec.presses > 30 || g_exec.stuck >= 4)
		{
			FinishMove(s, "error: could not move the cursor to that hand card (unusable card?)");
			return;
		}
		g_exec.lastIdx = s.selectedCardIdx;
		PressArrow(s.selectedCardIdx < m.card ? kArrow_Right : kArrow_Left);
		g_exec.presses++;
		g_exec.nextActionTick = now + 180;
		return;
	}

	case kPhase_WaitForMode:
	{
		UInt32 wanted = m.action == "discard_track" ? kFlag_IsDiscardTrack : kFlag_IsPlaceCard;
		if (s.flags & wanted)
		{
			g_exec.phase = kPhase_Navigate;
			g_exec.presses = g_exec.stuck = 0;
			g_exec.nextActionTick = now + 150;
		}
		else if (now > g_exec.deadline && g_exec.retries < 3)
		{
			// The game sometimes ignores the click, mostly on a card that was just drawn and is
			// still sliding into the hand. Click again instead of giving the turn back.
			g_exec.retries++;
			Log("Turn " + std::to_string(m.turn) + ": no reaction to the click, retry " + std::to_string(g_exec.retries));
			if (m.action == "discard_track")
			{
				ClickButton(kButton_CGM_DiscardTrack, kOff_Tile_CGM_DiscardTrackButton);
				g_exec.deadline = now + 2000;
				g_exec.nextActionTick = now + 200;
			}
			else
			{
				g_exec.phase = kPhase_SelectHandCard;
				g_exec.presses = g_exec.stuck = 0;
				g_exec.lastIdx = -99;
				g_exec.nextActionTick = now + 300;
			}
		}
		else if (now > g_exec.deadline)
		{
			FinishMove(s, "error: the game did not enter " + std::string(m.action == "discard_track" ? "discard-caravan" : "placing") + " mode");
		}
		return;
	}

	case kPhase_Navigate:
	{
		int curCol = TrackCol(s.selectedTrack), curY = ScreenY(s.selectedTrack, s.selectedRow);
		int tCol = TrackCol(m.track);
		bool sideOk = IsNpcTrack(s.selectedTrack) == IsNpcTrack(m.track);
		bool rowOk = m.row < 0 ? sideOk : curY == ScreenY(m.track, m.row);
		if (curCol == tCol && sideOk && rowOk)
		{
			ClickButton(kButton_CGM_SelectCard, kOff_Tile_CGM_SelectCardButton);
			char buf[96];
			sprintf_s(buf, "ok: %s track %d row %d", m.action == "discard_track" ? "discarded caravan" : "placed on", s.selectedTrack, s.selectedRow);
			FinishMove(s, buf);
			return;
		}
		bool moved = s.selectedTrack != g_exec.lastTrack || s.selectedRow != g_exec.lastRow;
		g_exec.stuck = moved ? 0 : g_exec.stuck + 1;
		if (g_exec.presses > 40 || g_exec.stuck >= 4)
		{
			char buf[128];
			sprintf_s(buf, "error: cursor stuck at track %d row %d (target track %d row %d); still in placing mode",
				s.selectedTrack, s.selectedRow, m.track, m.row);
			FinishMove(s, buf);
			return;
		}
		g_exec.lastTrack = s.selectedTrack;
		g_exec.lastRow = s.selectedRow;
		int tY = m.row < 0 ? (IsNpcTrack(m.track) ? -1 : 0) : ScreenY(m.track, m.row);
		// Cross to the other side first if needed; on the right side fix the column, then the row.
		// The cursor refuses slots the card cannot use, so when one axis is blocked, try the other.
		bool needVertical = !sideOk || !rowOk;
		bool needHorizontal = curCol != tCol;
		bool preferVertical = !sideOk || !needHorizontal;
		if (g_exec.stuck >= 2 && needVertical && needHorizontal)
			preferVertical = !preferVertical;
		if (preferVertical && needVertical)
			PressArrow(curY > tY ? kArrow_Up : kArrow_Down);
		else if (needHorizontal)
			PressArrow(curCol < tCol ? kArrow_Right : kArrow_Left);
		else
			PressArrow(curY > tY ? kArrow_Up : kArrow_Down);
		g_exec.presses++;
		g_exec.nextActionTick = now + 180;
		return;
	}

	default:
		return;
	}
}

// ---- Deck building: fill the deck to 30 cards, then press Play ---------------------------

// Clicking Add puts the card shown in the collection into the deck. When a click adds nothing
// (card already used, or the deck view is showing), step to the next card, and now and then
// switch back to the All Cards view, until the deck reaches 30 or we run out of attempts.
static void FillDeck(const Snapshot &s, DWORD now)
{
	if (now < g_deck.nextActionTick)
		return;
	if (s.cardsInDeck > g_deck.lastCount)
		g_deck.noProgress = 0;
	else if (g_deck.lastCount != (UInt32)-1)
		g_deck.noProgress++;
	g_deck.lastCount = s.cardsInDeck;

	if (g_deck.noProgress > 40)
	{
		g_deck.gaveUp = true;
		g_turn.lastResult = "could not fill the deck automatically; add cards and press Play yourself";
		Log("Deck building: gave up adding cards at " + std::to_string(s.cardsInDeck));
		return;
	}
	if (g_deck.noProgress == 0 || g_deck.noProgress % 2 == 0)
		ClickButton(kButton_CDM_Add, kOff_Tile_CDM_AddButton);
	else if (g_deck.noProgress % 6 == 5)
		ClickButton(kButton_CDM_AllCards, kOff_Tile_CDM_AllCardsButton);
	else
		PressArrow(kArrow_Right);
	g_deck.nextActionTick = now + 250;
}

static void HandleDeckScreen(const Snapshot &s, DWORD now)
{
	if (!s.ok || s.menu != kMenu_BuildDeck)
	{
		g_deckPlayClicked = false;
		g_deckEnteredTick = 0;
		g_deck = {};
		return;
	}
	if (!g_deckEnteredTick)
	{
		g_deckEnteredTick = now;
		Log("Deck building: " + std::to_string(s.cardsInDeck) + " of " + std::to_string(s.totalCards) + " cards in the deck");
	}
	// With the bot off you build your own deck and press Play yourself.
	if (!g_bot.enabled || g_deckPlayClicked || g_deck.gaveUp || s.stage != 0 || now - g_deckEnteredTick < 1500)
		return;
	if (s.cardsInDeck < kMinDeckSize)
	{
		if (s.totalCards < kMinDeckSize)
		{
			g_turn.lastResult = "you own fewer than 30 caravan cards";
			return;
		}
		FillDeck(s, now);
		return;
	}
	if (g_deck.lastCount != (UInt32)-1)
		Log("Deck building: filled the deck to " + std::to_string(s.cardsInDeck) + " cards");
	g_deckPlayClicked = true;
	ClickButton(kButton_CDM_Play, kOff_Tile_CDM_PlayButton);
	Log("Deck building: clicked Play with " + std::to_string(s.cardsInDeck) + " cards");
}

// ---- Bot toggle -------------------------------------------------------------------------

static void BotMessage(const std::string &msg, const char *sound)
{
	Log(msg);
	ShowGameMessage(msg.c_str());
	if (sound)
		PlayGameSound(sound);
}

static std::string IniString(const char *key)
{
	char buf[MAX_PATH] = {};
	GetPrivateProfileStringA("Bot", key, "", buf, sizeof(buf), g_iniPath.c_str());
	return buf;
}

static std::string KeyName(int key)
{
	char name[32] = {};
	// DirectInput key codes are keyboard scan codes.
	if (GetKeyNameTextA(key << 16, name, sizeof(name)) > 0)
		return name;
	return "key " + std::to_string(key);
}

static void SetBot(bool on)
{
	g_bot.enabled = on;
	// Replay the current turn's decision when the bot comes on mid-turn.
	g_bot.lastTurnId = 0;
	// A move written just before the switch must not be played.
	if (g_external)
		DeleteFileA((g_outDir + "\\move.json").c_str());
	if (on)
		BotMessage("Caravan bot: ON (" + KeyName(g_bot.key) + " to stop)", "UIMenuOK");
	else
		BotMessage("Caravan bot: OFF (" + KeyName(g_bot.key) + " to start)", "UIMenuCancel");
}

// Runs every frame: the key is only read while the Caravan menu is open.
static void PollToggleKey()
{
	bool down = GetMenu() && IsKeyHeld(g_bot.key);
	if (down && !g_bot.keyWasDown)
		SetBot(!g_bot.enabled);
	g_bot.keyWasDown = down;
}

static void ShowHint(const Snapshot &s)
{
	if (s.active && !g_bot.enabled && !g_bot.hintShown)
	{
		g_bot.hintShown = true;
		ShowGameMessage(("Caravan bot: press " + KeyName(g_bot.key) + " to let it play").c_str());
	}
}

// ---- Main loop --------------------------------------------------------------------------

static void OnMessage(NVSEMessagingInterface::Message *msg)
{
	if (msg->type != kMessage_MainGameLoop)
		return;
	PollToggleKey();  // every frame, so a short key press is not missed
	if (++g_frame % 3)
		return;
	DWORD now = GetTickCount();
	Snapshot snap;
	bool readOk = TakeSnapshot(snap);
	if (!readOk)
		snap.ok = false;
	ShowHint(snap);

	if (!snap.active)
	{
		g_exec.phase = kPhase_Idle;
		g_turn.awaiting = false;
	}
	else
	{
		UpdateTurn(snap, now);
		HandleDeckScreen(snap, now);
		if (g_bot.enabled)
			BotPlayTurn(snap, now);
		else if (g_external)
			TryExternalMove(now);
		StepMove(snap, now);
	}

	if (!g_external)
	{
		static bool readFailLogged = false;
		if (!readOk && !readFailLogged)
			Log("Read of the Caravan menu failed (exception or bad array size).");
		readFailLogged = !readOk;
		return;
	}
	std::string json = SnapshotJson(snap);
	if (json == g_lastJson)
		return;
	if (!readOk && g_lastJson.find("\"error\"") == std::string::npos)
		Log("Read of the Caravan menu failed (exception or bad array size).");
	g_lastJson = json;
	WriteState(json);
}

extern "C" __declspec(dllexport) bool NVSEPlugin_Query(const NVSEInterface *nvse, PluginInfo *info)
{
	info->infoVersion = PluginInfo::kInfoVersion;
	info->name = "CaravanBot";
	info->version = 6;
	return !nvse->isEditor;
}

extern "C" __declspec(dllexport) bool NVSEPlugin_Load(const NVSEInterface *nvse)
{
	g_pluginHandle = nvse->GetPluginHandle();
	g_gameDir = nvse->GetRuntimeDirectory();
	while (!g_gameDir.empty() && (g_gameDir.back() == '\\' || g_gameDir.back() == '/'))
		g_gameDir.pop_back();
	g_outDir = g_gameDir + "\\CaravanBot";
	CreateDirectoryA(g_outDir.c_str(), nullptr);

	// The INI sits next to this DLL, wherever the mod manager put it.
	HMODULE self = nullptr;
	char dllPath[MAX_PATH] = {};
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCSTR)&NVSEPlugin_Load, &self);
	GetModuleFileNameA(self, dllPath, sizeof(dllPath));
	g_iniPath = dllPath;
	g_iniPath = g_iniPath.substr(0, g_iniPath.find_last_of("\\/") + 1) + "CaravanBot.ini";

	// A fresh log each game session.
	FILE *f = nullptr;
	if (fopen_s(&f, (g_outDir + "\\CaravanBot.log").c_str(), "w") == 0 && f)
		fclose(f);

	std::string key = IniString("ToggleKey");
	if (!key.empty())
		g_bot.key = (int)strtol(key.c_str(), nullptr, 0);
	g_external = strtol(IniString("ExternalControl").c_str(), nullptr, 0) != 0;
	if (g_external)
		DeleteFileA((g_outDir + "\\move.json").c_str());

	auto *messaging = (NVSEMessagingInterface *)nvse->QueryInterface(kInterface_Messaging);
	if (!messaging || !messaging->RegisterListener(g_pluginHandle, "NVSE", OnMessage))
	{
		Log("Could not register for NVSE messages.");
		return false;
	}
	Log("CaravanBot 1.0.0 loaded (toggle key " + KeyName(g_bot.key) + (g_external ? ", external control on" : "") + ").");
	return true;
}
