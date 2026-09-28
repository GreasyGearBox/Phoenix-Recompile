// Deeps 1.06 - Ashita 4.30 / Phoenix compatibility port.
//
// Core packet parsing and damage-accounting behavior is based directly on:
//   https://github.com/relliko/Deeps (Deeps 1.06, GPLv3)
//
// This build targets the Ashita 4.30 plugin ABI used by Phoenix XI.  The build
// environment used here does not provide the MSVC runtime / STL, so the original
// dynamic STL containers are represented by fixed-capacity internal storage.
// The important 0x28 action event path, full entity lookup, pet-owner attribution,
// damage categories, additional effects and skillchain accounting are preserved.
//
// Unlike the previous failed compatibility attempt, this version DOES NOT impose
// a 46-byte minimum on action records.  Normal one-hit 0x28 packets are ~35 bytes.

// ---- Minimal Win32 / Ashita ABI types -------------------------------------------------
typedef unsigned char      uint8_t;
typedef signed char        int8_t;
typedef unsigned short     uint16_t;
typedef signed short       int16_t;
typedef unsigned int       uint32_t;
typedef signed int         int32_t;
typedef unsigned long long uint64_t;
typedef unsigned int       uintptr_t;
typedef unsigned int       size_t;
typedef unsigned int       UINT;
typedef unsigned int       DWORD;
typedef unsigned int       WPARAM;
typedef int                LPARAM;
typedef void*              HWND;

struct RECT { long left; long top; long right; long bottom; };
struct RGNDATA { uint8_t opaque[1]; };
struct IDirect3DDevice8 {};
typedef int D3DRENDERSTATETYPE;
typedef int D3DPRIMITIVETYPE;
typedef int D3DFORMAT;

struct IAshitaCore;
struct ILogManager;

// Ashita 4.30 IPluginBase ABI. Order matches plugins/sdk/Ashita.h.
struct IPluginBase
{
    virtual const char* GetName(void) const = 0;
    virtual const char* GetAuthor(void) const = 0;
    virtual const char* GetDescription(void) const = 0;
    virtual const char* GetLink(void) const = 0;
    virtual double GetVersion(void) const = 0;
    virtual double GetInterfaceVersion(void) const = 0;
    virtual int32_t GetPriority(void) const = 0;
    virtual uint32_t GetFlags(void) const = 0;
    virtual bool Initialize(IAshitaCore*, ILogManager*, uint32_t) = 0;
    virtual void Release(void) = 0;
    virtual void HandleEvent(const char*, const void*, uint32_t) = 0;
    virtual bool HandleCommand(int32_t, const char*, bool) = 0;
    virtual bool HandleIncomingText(int32_t, bool, const char*, int32_t*, bool*, char*, bool, bool) = 0;
    virtual bool HandleOutgoingText(int32_t, const char*, int32_t*, char*, bool, bool) = 0;
    virtual bool HandleIncomingPacket(uint16_t, uint32_t, const uint8_t*, uint8_t*, uint32_t, const uint8_t*, bool, bool) = 0;
    virtual bool HandleOutgoingPacket(uint16_t, uint32_t, const uint8_t*, uint8_t*, uint32_t, const uint8_t*, bool, bool) = 0;
    virtual bool Direct3DInitialize(IDirect3DDevice8*) = 0;
    virtual void Direct3DBeginScene(bool) = 0;
    virtual void Direct3DEndScene(bool) = 0;
    virtual void Direct3DPresent(const RECT*, const RECT*, HWND, const RGNDATA*) = 0;
    virtual bool Direct3DSetRenderState(D3DRENDERSTATETYPE, DWORD*) = 0;
    virtual bool Direct3DDrawPrimitive(D3DPRIMITIVETYPE, UINT, UINT) = 0;
    virtual bool Direct3DDrawIndexedPrimitive(D3DPRIMITIVETYPE, UINT, UINT, UINT, UINT) = 0;
    virtual bool Direct3DDrawPrimitiveUP(D3DPRIMITIVETYPE, UINT, const void*, UINT) = 0;
    virtual bool Direct3DDrawIndexedPrimitiveUP(D3DPRIMITIVETYPE, UINT, UINT, UINT, const void*, D3DFORMAT, const void*, UINT) = 0;
};

class IPlugin : public IPluginBase
{
public:
    virtual ~IPlugin(void) {}
};

// ---- Minimal runtime ------------------------------------------------------------------
extern "C" void* memset(void* dst, int c, size_t n)
{
    uint8_t* d = (uint8_t*)dst;
    while (n--) *d++ = (uint8_t)c;
    return dst;
}
extern "C" void* memcpy(void* dst, const void* src, size_t n)
{
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dst;
}
extern "C" int memcmp(const void* a, const void* b, size_t n)
{
    const uint8_t* x = (const uint8_t*)a;
    const uint8_t* y = (const uint8_t*)b;
    while (n--) { if (*x != *y) return (int)*x - (int)*y; ++x; ++y; }
    return 0;
}
void* operator new(size_t, void* p) { return p; }
void operator delete(void*) noexcept {}
void operator delete(void*, size_t) noexcept {}
extern "C" int _fltused = 0;

static char lowerc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static bool streq(const char* a, const char* b)
{
    if (!a || !b) return false;
    while (*a && *b) { if (*a++ != *b++) return false; }
    return *a == 0 && *b == 0;
}
static bool streqi(const char* a, const char* b)
{
    if (!a || !b) return false;
    while (*a && *b) { if (lowerc(*a++) != lowerc(*b++)) return false; }
    return *a == 0 && *b == 0;
}
static void copy_text(char* dst, uint32_t cap, const char* src)
{
    if (!dst || cap == 0) return;
    uint32_t i = 0;
    if (src) while (src[i] && i + 1 < cap) { dst[i] = src[i]; ++i; }
    dst[i] = 0;
}
static const char* skip_spaces(const char* p) { while (p && (*p == ' ' || *p == '\t')) ++p; return p; }
static const char* next_token(const char* p, char* out, uint32_t cap)
{
    p = skip_spaces(p); uint32_t n = 0;
    while (p && *p && *p != ' ' && *p != '\t') { if (n + 1 < cap) out[n++] = *p; ++p; }
    if (cap) out[n] = 0;
    return p;
}
static int32_t parse_int(const char* s, int32_t def)
{
    s = skip_spaces(s); if (!s || !*s) return def;
    bool neg = false; if (*s == '-') { neg = true; ++s; }
    int32_t v = 0; bool any = false;
    while (*s >= '0' && *s <= '9') { any = true; v = v * 10 + (*s++ - '0'); }
    return any ? (neg ? -v : v) : def;
}

template<typename R, typename... A>
static R vcall(void* obj, uint32_t index, A... args)
{
    void** table = *(void***)obj;
    typedef R (__thiscall *Fn)(void*, A...);
    Fn fn = (Fn)table[index];
    return fn(obj, args...);
}

// ---- Ashita 4.30 interface slots -------------------------------------------------------
// IAshitaCore: public 4.30 SDK ordering.
enum CoreSlot {
    CORE_INSTALL_PATH = 1,
    CORE_CHAT = 4,
    CORE_CONFIG = 5,
    CORE_FONT = 6,
    CORE_INPUT = 8,
    CORE_MEMORY = 9,
    CORE_RESOURCE = 16
};
// IMemoryManager ordering in Ashita 4.30.
enum MemorySlot { MEMORY_ENTITY = 2, MEMORY_PARTY = 4 };
// IEntity 4.30 ordering, counted directly from plugins/sdk/Ashita.h.
enum EntitySlot {
    ENTITY_MAP_SIZE = 1,
    ENTITY_SERVER_ID = 28,
    ENTITY_NAME = 29,
    ENTITY_PET_TARGET_INDEX = 115,
    ENTITY_TRUST_OWNER_TARGET_INDEX = 134
};
// IParty slots used by the old renderer / party-only filter.
enum PartySlot { PARTY_NAME = 15, PARTY_SERVERID = 16, PARTY_MAINJOB = 32 };
// IResourceManager 4.30 method ordering.
enum ResourceSlot { RES_ABILITY_ID = 0, RES_SPELL_ID = 3 };
enum ChatSlot { CHAT_QUEUE = 1, CHAT_WRITE = 2 };
enum ConfigSlot { CFG_LOAD = 0, CFG_SAVE = 1, CFG_GETSTRING = 5, CFG_SETVALUE = 6, CFG_GETBOOL = 7, CFG_GETFLOAT = 16 };
enum FontMgrSlot { FM_CREATE = 0, FM_GET = 1, FM_DELETE = 2 };
enum FontSlot {
    FONT_GET_BACKGROUND = 4,
    FONT_GET_ALIAS = 7,
    FONT_SET_VISIBLE = 35,
    FONT_SET_CANFOCUS = 36,
    FONT_SET_LOCKED = 37,
    FONT_SET_FONTFAMILY = 43,
    FONT_SET_FONTHEIGHT = 44,
    FONT_SET_CREATEFLAGS = 45,
    FONT_SET_DRAWFLAGS = 46,
    FONT_SET_BOLD = 47,
    FONT_SET_COLOR = 52,
    FONT_SET_COLOR_OUTLINE = 53,
    FONT_SET_PADDING = 54,
    FONT_SET_POSX = 55,
    FONT_SET_POSY = 56,
    FONT_SET_AUTORESIZE = 57,
    FONT_SET_TEXT = 60
};
enum PrimitiveSlot {
    PRIM_SET_BORDER_VISIBLE = 29,
    PRIM_SET_VISIBLE = 33,
    PRIM_SET_CANFOCUS = 36,
    PRIM_SET_WIDTH = 41,
    PRIM_SET_HEIGHT = 42,
    PRIM_SET_COLOR = 44
};

// Current Ashita 4.30 resource structures. Only Name[2] is consumed.
struct IAbility
{
    uint16_t Id; uint8_t Type; uint8_t Element; uint16_t ListIconId; uint16_t ManaCost;
    uint16_t RecastTimerId; uint16_t Targets; int16_t TPCost; uint8_t MenuCategoryId;
    uint8_t MonsterLevel; int8_t Range; uint8_t AreaRange; uint8_t AreaShapeType;
    uint8_t CursorTargetType; uint8_t Unknown0000; uint8_t Unknown0001;
    uint16_t Unknown0002; uint16_t Unknown0003; uint16_t Unknown0004; uint16_t Unknown0005;
    uint16_t Unknown0006; uint16_t Unknown0007; uint16_t Unknown0008; uint8_t Unknown0009[11];
    uint8_t EOE; const char* Name[3]; const char* Description[3];
};
struct ISpell
{
    uint16_t Index; uint16_t Type; uint16_t Element; uint16_t Targets; uint16_t Skill; uint16_t ManaCost;
    uint8_t CastTime; uint8_t RecastDelay; int16_t LevelRequired[24]; uint16_t Id;
    uint16_t ListIconNQ; uint16_t ListIconHQ; uint8_t Requirements; int8_t Range;
    uint8_t AreaRange; uint8_t AreaShapeType; uint8_t CursorTargetType; uint8_t Unknown0000[3];
    uint32_t AreaFlags; uint16_t Unknown0001; uint8_t Unknown0002; uint8_t Unknown0003;
    uint8_t Unknown0004[8]; uint32_t JobPointMask; uint8_t Unknown0005[3]; uint8_t EOE;
    const char* Name[3]; const char* Description[3];
};

// ---- Packet helpers -------------------------------------------------------------------
static uint32_t read32(const uint8_t* p, uint32_t pos)
{
    p += pos;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Matches Ashita::BinaryData::UnpackBitsBE usage for FFXI action packets: bit offsets
// advance from the least-significant bit within little-endian packet bytes.
static uint32_t unpack_bits_be(const uint8_t* data, uint32_t offset, uint8_t len)
{
    uint32_t byteOffset = offset >> 3;
    uint32_t bitOffset = offset & 7;
    uint64_t raw = 0;
    uint32_t bytes = (bitOffset + len + 7) >> 3;
    if (bytes > 8) bytes = 8;
    for (uint32_t i = 0; i < bytes; ++i) raw |= ((uint64_t)data[byteOffset + i]) << (i * 8);
    uint64_t mask = (len == 32) ? 0xFFFFFFFFULL : ((1ULL << len) - 1ULL);
    return (uint32_t)((raw >> bitOffset) & mask);
}
static bool bits_available(uint32_t packetSize, uint32_t bitOffset, uint32_t bitCount)
{
    return ((uint64_t)bitOffset + (uint64_t)bitCount) <= ((uint64_t)packetSize * 8ULL);
}

// ---- Original Deeps constants ----------------------------------------------------------
enum ACTIONTYPE {
    ACTIONTYPE_MELEE            = 0x01,
    ACTIONTYPE_RA_FINISH        = 0x02,
    ACTIONTYPE_WS_FINISH        = 0x03,
    ACTIONTYPE_CAST_FINISH      = 0x04,
    ACTIONTYPE_ITEM_FINISH      = 0x05,
    ACTIONTYPE_JA               = 0x06,
    ACTIONTYPE_WS_START         = 0x07,
    ACTIONTYPE_CAST_START       = 0x08,
    ACTIONTYPE_ITEM_START       = 0x09,
    ACTIONTYPE_NPC_TP_FINISH    = 0x0B,
    ACTIONTYPE_RA_START         = 0x0C,
    ACTIONTYPE_AVATAR_BP_FINISH = 0x0D,
    ACTIONTYPE_JA_DNC           = 0x0E,
    ACTIONTYPE_JA_RUN           = 0x0F
};
static const uint16_t MSG_ADD_EFFECT_DMG  = 163;
static const uint16_t MSG_ADD_EFFECT_DMG2 = 229;

static const uint16_t kHitMessages[]   = {1,2,77,132,157,161,163,185,187,197,227,264,281,317,352,413,522,576,577};
static const uint16_t kCritMessages[]  = {67,252,265,274,353,379};
static const uint16_t kMissMessages[]  = {15,85,158,188,245,284,324,354};
static const uint16_t kEvadeMessages[] = {14,30,31,32,33,189,248,282,283,323,355};
static const uint16_t kParryMessages[] = {69,70};

static bool has_msg(const uint16_t* a, uint32_t n, uint16_t v)
{
    for (uint32_t i = 0; i < n; ++i) if (a[i] == v) return true;
    return false;
}

static const uint32_t kJobColors[24] = {
    0xFF191970u,0xFFFF0000u,0xFFFF8C00u,0xFFFFFFFFu,0xFF4B0082u,0xFFFF69B4u,
    0xFF228B22u,0xFFD3D3D3u,0xFF444444u,0xFF8B4513u,0xFFFFFF00u,0xFFADFF2Fu,
    0xFF800000u,0xFF708090u,0xFF9370DBu,0xFF00FFFFu,0xFF4169E1u,0xFFFFD8B1u,
    0xFFFFFAC8u,0xFFE6194Bu,0xFFCD853Fu,0xFF808000u,0xFF00FF00u,0xFF007C5Cu
};
static const uint32_t kRandomColors[] = {
    0xFF0C009Bu,0xFF8C0000u,0xFFFFB120u,0xFF8F8F8Fu,0xFF444444u,
    0xFFFF0000u,0xFF00A431u,0xFFC6C600u,0xFF740091u,0xFFA5990Au,
    0xFFB8800Au,0xFFE000E6u,0xFFEA6400u,0xFF770000u,0xFF8211FFu,
    0xFF4FC400u,0xFF0010D9u,0xFF884400u,0xFFF46200u,0xFF0FBEDCu,0xFF007B91u
};

// ---- Fixed-capacity representation of original STL data -------------------------------
enum DamageKind { DK_HIT=0, DK_CRIT=1, DK_MISS=2, DK_EVADE=3, DK_PARRY=4, DK_COUNT=5 };

struct DamageStat
{
    uint64_t total;
    uint32_t max;
    uint32_t min;
    uint32_t count;
};

struct DamageSource
{
    bool used;
    bool isMagic;
    uint32_t key;
    char name[48];
    DamageStat damage[DK_COUNT];
};

static const uint32_t MAX_SOURCES = 64;
struct EntitySources
{
    bool used;
    char name[32];
    uint32_t color;
    uint32_t id;
    uint32_t ownerid; // nonzero only for pet entries
    DamageSource sources[MAX_SOURCES];
};

static const uint32_t MAX_ENTITIES = 192;
static const uint32_t MAX_PACKET_HISTORY = 200;
static const uint32_t MAX_PACKET_BYTES = 1024;
struct PacketHistory
{
    bool used;
    uint16_t size;
    uint8_t data[MAX_PACKET_BYTES];
};

static const char* kRowAliases[15] = {
    "Deeps430Row00","Deeps430Row01","Deeps430Row02","Deeps430Row03","Deeps430Row04",
    "Deeps430Row05","Deeps430Row06","Deeps430Row07","Deeps430Row08","Deeps430Row09",
    "Deeps430Row10","Deeps430Row11","Deeps430Row12","Deeps430Row13","Deeps430Row14"
};


static uint64_t u64_divmod(uint64_t n, uint64_t d, uint64_t* rem)
{
    if (d == 0) { if (rem) *rem = 0; return 0; }
    uint64_t q = 0, r = 0;
    for (int i = 63; i >= 0; --i) {
        r = (r << 1) | ((n >> i) & 1ULL);
        if (r >= d) { r -= d; q |= (1ULL << i); }
    }
    if (rem) *rem = r;
    return q;
}
static uint64_t u64_div(uint64_t n, uint64_t d) { return u64_divmod(n,d,0); }
static uint64_t u64_mod(uint64_t n, uint64_t d) { uint64_t r=0; u64_divmod(n,d,&r); return r; }

// ---- Formatting helpers ---------------------------------------------------------------
static uint32_t append_str(char* out, uint32_t pos, uint32_t cap, const char* s)
{
    if (!s) return pos;
    while (*s && pos + 1 < cap) out[pos++] = *s++;
    out[pos] = 0; return pos;
}
static uint32_t append_u32(char* out, uint32_t pos, uint32_t cap, uint32_t v)
{
    char tmp[16]; uint32_t n = 0;
    if (v == 0) tmp[n++] = '0';
    else while (v && n < 15) { tmp[n++] = (char)('0' + (uint32_t)u64_mod(v,10)); v = u64_div(v,10); }
    while (n && pos + 1 < cap) out[pos++] = tmp[--n];
    out[pos] = 0; return pos;
}
static uint32_t append_u64(char* out, uint32_t pos, uint32_t cap, uint64_t v)
{
    char tmp[32]; uint32_t n = 0;
    if (v == 0) tmp[n++] = '0';
    else while (v && n < 31) { tmp[n++] = (char)('0' + (uint32_t)u64_mod(v,10)); v = u64_div(v,10); }
    while (n && pos + 1 < cap) out[pos++] = tmp[--n];
    out[pos] = 0; return pos;
}
static uint32_t append_pad_name(char* out, uint32_t pos, uint32_t cap, const char* s, uint32_t width)
{
    uint32_t n = 0; while (s && *s && n < width && pos + 1 < cap) { out[pos++] = *s++; ++n; }
    while (n++ < width && pos + 1 < cap) out[pos++] = ' ';
    out[pos] = 0; return pos;
}
static uint32_t append_pct10(char* out, uint32_t pos, uint32_t cap, uint32_t tenth)
{
    pos = append_u32(out,pos,cap,tenth/10); if (pos+1<cap) out[pos++]='.';
    if (pos+1<cap) out[pos++]=(char)('0'+(tenth%10)); if(pos+1<cap) out[pos++]='%'; out[pos]=0; return pos;
}
static void append_action_id(char* out, uint32_t cap, const char* label, uint32_t id)
{
    uint32_t p=0; out[0]=0; p=append_str(out,p,cap,label); p=append_str(out,p,cap," "); append_u32(out,p,cap,id);
}

class Deeps430 : public IPlugin
{
public:
    IAshitaCore* core;
    ILogManager* log;
    uint32_t pluginId;
    void* title;
    void* rows[15];

    EntitySources entities[MAX_ENTITIES];
    PacketHistory packetHistory[MAX_PACKET_HISTORY];
    uint32_t packetHistoryPos;

    bool debug;
    bool partyOnly;
    bool jobColors;
    bool countSkillchains;
    bool tvMode;
    bool visible;
    float xpos;
    float ypos;

    // Runtime instrumentation. /dps status reports exactly how far packets get.
    uint32_t statPacket28;
    uint32_t statDuplicate;
    uint32_t statBadHeader;
    uint32_t statNoEntityIndex;
    uint32_t statNpcRejected;
    uint32_t statEntityFull;
    uint32_t statParsedActions;
    uint32_t statRecordedActions;
    uint32_t statUnknownMessage;
    uint32_t statShortPacket;

    // Last-seen packet diagnostics. These make one runtime test sufficient to
    // distinguish callback, header, entity-resolution and message-table failures.
    uint32_t lastPacketSize;
    uint32_t lastUserId;
    uint32_t lastTargetCount;
    uint32_t lastActionType;
    uint32_t lastActionId;
    uint32_t lastActionCount;
    uint32_t lastEntityIndex;
    uint32_t lastMessageId;
    uint32_t lastDamage;

    Deeps430()
    {
        core = 0; log = 0; pluginId = 0; title = 0; packetHistoryPos = 0;
        debug = false; partyOnly = true; jobColors = true; countSkillchains = true; tvMode = false; visible = true;
        xpos = 300.0f; ypos = 300.0f;
        for (uint32_t i=0;i<15;++i) rows[i]=0;
        memset(entities,0,sizeof(entities));
        memset(packetHistory,0,sizeof(packetHistory));
        clear_stats();
    }

    const char* GetName(void) const override { return "Deeps"; }
    const char* GetAuthor(void) const override { return "Relliko, kjLotus; Ashita 4.30 compatibility port"; }
    const char* GetDescription(void) const override { return "Deeps damage meter port for Ashita 4.30 / Phoenix XI."; }
    const char* GetLink(void) const override { return "https://github.com/relliko/Deeps"; }
    double GetVersion(void) const override { return 1.06430; }
    double GetInterfaceVersion(void) const override { return 4.30; }
    int32_t GetPriority(void) const override { return 0; }
    // Ashita::PluginFlags::LegacyDirect3D == commands|text|packets|D3D == 0x0F.
    uint32_t GetFlags(void) const override { return 0x0Fu; }

    bool Initialize(IAshitaCore* c, ILogManager* l, uint32_t id) override
    {
        core=c; log=l; pluginId=id;
        void* cfg = core ? vcall<void*>(core, CORE_CONFIG) : 0;
        if (cfg) vcall<bool>(cfg, CFG_LOAD, "Deeps", "Deeps");
        write_chat("[Deeps] Ashita 4.30 core parser port loaded. /dps status is available.");
        return core != 0;
    }
    void Release(void) override
    {
        save_config();
        void* fm = core ? vcall<void*>(core, CORE_FONT) : 0;
        if (fm) {
            if (title) vcall<void>(fm, FM_DELETE, "Deeps430Title");
            for (uint32_t i=0;i<15;++i) if (rows[i]) vcall<void>(fm, FM_DELETE, kRowAliases[i]);
        }
        title=0; for(uint32_t i=0;i<15;++i) rows[i]=0;
    }
    void HandleEvent(const char*, const void*, uint32_t) override {}

    bool HandleCommand(int32_t, const char* command, bool) override
    {
        if (!command) return false;
        const char* p = command; char tok[32];
        p = next_token(p,tok,sizeof(tok));
        if (!streqi(tok,"/dps") && !streqi(tok,"/deeps")) return false;
        p = next_token(p,tok,sizeof(tok));
        if (!tok[0] || streqi(tok,"help")) { print_help(); return true; }
        if (streqi(tok,"reset")) { reset_damage(); write_chat("[Deeps] Damage counters reset."); return true; }
        if (streqi(tok,"status")) { print_status(); return true; }
        if (streqi(tok,"debug")) { debug=!debug; write_chat(debug?"[Deeps] Debug enabled.":"[Deeps] Debug disabled."); return true; }
        if (streqi(tok,"partyonly")) { partyOnly=!partyOnly; write_chat(partyOnly?"[Deeps] Party Only Enabled":"[Deeps] Party Only Disabled"); return true; }
        if (streqi(tok,"jobcolors")) { jobColors=!jobColors; write_chat(jobColors?"[Deeps] Job Colors Enabled":"[Deeps] Job Colors Disabled"); return true; }
        if (streqi(tok,"sc")) { countSkillchains=!countSkillchains; write_chat(countSkillchains?"[Deeps] Skillchain Damage Enabled":"[Deeps] Skillchain Damage Disabled"); return true; }
        if (streqi(tok,"tvmode")) { tvMode=!tvMode; write_chat(tvMode?"[Deeps] TV Mode Enabled":"[Deeps] TV Mode Disabled"); return true; }
        if (streqi(tok,"hide")) { visible=false; set_all_visible(false); return true; }
        if (streqi(tok,"show")) { visible=true; set_all_visible(true); return true; }
        if (streqi(tok,"report")) { report_local(); return true; }
        if (streqi(tok,"pos")) {
            char a[24],b[24]; p=next_token(p,a,sizeof(a)); p=next_token(p,b,sizeof(b));
            xpos=(float)parse_int(a,(int32_t)xpos); ypos=(float)parse_int(b,(int32_t)ypos); update_positions();
            write_chat("[Deeps] Position updated."); return true;
        }
        print_help(); return true;
    }
    bool HandleIncomingText(int32_t, bool, const char*, int32_t*, bool*, char*, bool, bool) override { return false; }
    bool HandleOutgoingText(int32_t, const char*, int32_t*, char*, bool, bool) override { return false; }

    bool HandleIncomingPacket(uint16_t id, uint32_t size, const uint8_t* data, uint8_t*, uint32_t, const uint8_t*, bool, bool blocked) override
    {
        if (blocked || id != 0x28 || !data) return false;
        statPacket28++;

        // Smallest useful 0x28 packet must include actor, target count, category/param,
        // and the first target/action header.  Do NOT use the previous bogus 46-byte guard.
        if (size < 24) { statShortPacket++; return false; }
        if (is_duplicate_packet(data,size)) { statDuplicate++; return false; }

        // Canonical 0x028 base layout (Windower/Ashita parsers): actor at byte 5,
        // target-count bitfield begins at bit 72, category at 82 and Param at 86.
        // Deeps 1.06 read only 8 target-count bits and 10 Param bits; reading the
        // full fields here is backwards-compatible while avoiding truncation.
        uint16_t targetNum = (uint16_t)unpack_bits_be(data,72,10);
        uint8_t actionType = (uint8_t)unpack_bits_be(data,82,4);
        uint16_t actionID  = (uint16_t)unpack_bits_be(data,86,16);
        uint8_t actionNum  = (uint8_t)unpack_bits_be(data,182,4);
        uint32_t userID    = read32(data,0x05);

        lastPacketSize=size; lastUserId=userID; lastTargetCount=targetNum;
        lastActionType=actionType; lastActionId=actionID; lastActionCount=actionNum;

        // Auto-attacks / ranged attacks do not require a meaningful Param value;
        // do not discard them merely because Param is zero. This is deliberately
        // more robust than Deeps 1.06's blanket actionID==0 rejection.
        bool needsActionId = !(actionType==ACTIONTYPE_MELEE || actionType==ACTIONTYPE_RA_FINISH);
        if (userID == 0 || actionType == 0 || targetNum == 0 || targetNum > 64 || actionNum == 0 ||
            (needsActionId && actionID == 0) || !is_parsed_action_type(actionType)) {
            statBadHeader++;
            return false;
        }

        uint16_t index = get_index_from_id(userID);
        lastEntityIndex=index;
        if (index == 0) { statNoEntityIndex++; return false; }

        EntitySources* entityInfo = find_entity(userID);
        if (entityInfo) {
            // Original Deeps checks for an owner's pet on subsequent actions and stores
            // a pet entity so later pet packets can be attributed to the owner.
            register_pet_for_owner(entityInfo,index,userID);
        } else {
            // Original Deeps ignores NPC actors here. Pets are admitted through the
            // owner-registration path above.
            if (userID > 0x01000000u) { statNpcRejected++; return false; }
            entityInfo = create_entity(userID,0,index);
            if (!entityInfo) { statEntityFull++; return false; }
        }

        bool isPet = entityInfo->ownerid != 0;
        if (isPet) {
            // Refresh pet owner because server ids can be reused after re-summons.
            void* em = entity_manager();
            if (!em) return false;
            uint16_t petOwnerIndex = vcall<uint16_t>(em,ENTITY_TRUST_OWNER_TARGET_INDEX,(uint32_t)index);
            if (petOwnerIndex != 0) {
                uint32_t ownerId = vcall<uint32_t>(em,ENTITY_SERVER_ID,(uint32_t)petOwnerIndex);
                if (ownerId) entityInfo->ownerid = ownerId;
            }
            EntitySources* owner = find_entity(entityInfo->ownerid);
            if (!owner) return false;
            entityInfo = owner;
        }

        DamageSource* source = get_damage_source(entityInfo,actionType,actionID,isPet);
        if (!source) return false;

        // This is the Deeps 1.06 action layout. startBit points at the target header;
        // action fields are offsets from it.  Valid one-hit packets can be ~35 bytes.
        uint32_t startBit = 150;
        for (uint32_t i=0; i<targetNum; ++i) {
            for (uint32_t j=0; j<actionNum; ++j) {
                // Need only through the additional-effect flag for the base action.
                if (!bits_available(size,startBit+121,1)) { statShortPacket++; return false; }

                uint8_t reaction      = (uint8_t)unpack_bits_be(data,startBit+36,5);
                uint16_t animation    = (uint16_t)unpack_bits_be(data,startBit+41,12);
                uint8_t specEffect    = (uint8_t)unpack_bits_be(data,startBit+53,7);
                uint32_t mainDamage   = (uint32_t)unpack_bits_be(data,startBit+63,17);
                uint16_t messageID    = (uint16_t)unpack_bits_be(data,startBit+80,10);
                uint8_t hasAdditional = (uint8_t)(unpack_bits_be(data,startBit+121,1)&1u);

                lastMessageId=messageID; lastDamage=mainDamage;
                (void)reaction; (void)specEffect;
                statParsedActions++;

                DamageSource* useSource = source;
                // Daken is encoded as a melee-category action with animation 4.
                if (actionType == ACTIONTYPE_MELEE && animation == 4)
                    useSource = get_damage_source(entityInfo,ACTIONTYPE_RA_FINISH,actionID,isPet);

                if (update_damage_source(useSource,messageID,mainDamage)) statRecordedActions++;
                else statUnknownMessage++;

                // Base action is 86 bits after the target's 36-bit header, then one
                // additional-effect flag.  Preserve Deeps field offsets, but advance
                // the variable portions correctly so multi-action packets stay aligned.
                uint32_t cursor = startBit + 121;
                cursor += 1; // additional effect flag

                if (hasAdditional) {
                    // Additional block: 10 effect/animation bits + 17 param + 10 message.
                    if (!bits_available(size,cursor,37)) { statShortPacket++; return false; }
                    cursor += 10;
                    uint32_t addEffectDamage = unpack_bits_be(data,cursor,17);
                    cursor += 17;
                    uint16_t addMessageID = (uint16_t)unpack_bits_be(data,cursor,10);
                    cursor += 10;

                    bool isSC = (addMessageID >= 288 && addMessageID <= 302);
                    if (actionType != ACTIONTYPE_JA &&
                        (addMessageID == MSG_ADD_EFFECT_DMG || addMessageID == MSG_ADD_EFFECT_DMG2 || (isSC && countSkillchains))) {
                        uint32_t key = (addMessageID == MSG_ADD_EFFECT_DMG || addMessageID == MSG_ADD_EFFECT_DMG2) ? (1u<<8) : (2u<<8);
                        const char* nm = (key==(1u<<8)) ? "Additional Effect" : "Skillchain";
                        DamageSource* addSource = get_special_source(entityInfo,key,nm);
                        if (addSource) {
                            add_damage_stat(&addSource->damage[DK_HIT],addEffectDamage);
                            statRecordedActions++;
                        }
                    }
                }

                // Spikes flag follows the optional additional-effect block.
                if (!bits_available(size,cursor,1)) { statShortPacket++; return false; }
                uint8_t hasSpikes = (uint8_t)(unpack_bits_be(data,cursor,1)&1u);
                cursor += 1;
                if (hasSpikes) {
                    if (!bits_available(size,cursor,34)) { statShortPacket++; return false; }
                    cursor += 34;
                }

                // cursor is absolute end of this action. Convert back to the target-base
                // representation expected by the next loop iteration: next reaction is +36.
                // A no-extra action ends at startBit+123, while the old increment form
                // effectively advances startBit by 87. (123-36 = 87.)
                startBit = cursor - 36;
            }
            // Next target header begins after the current action list. The original
            // parser advances another 36 bits between targets.
            startBit += 36;

            // For multi-target packets each target carries its own 4-bit action count.
            // The legacy code reused the first count; refresh it when the next target
            // header is present so AoE actions remain aligned.
            if (i + 1 < targetNum && bits_available(size,startBit+32,4))
                actionNum = (uint8_t)unpack_bits_be(data,startBit+32,4);
        }
        return false;
    }

    bool HandleOutgoingPacket(uint16_t, uint32_t, const uint8_t*, uint8_t*, uint32_t, const uint8_t*, bool, bool) override { return false; }

    bool Direct3DInitialize(IDirect3DDevice8*) override
    {
        load_config();
        void* fm=vcall<void*>(core,CORE_FONT); if(!fm) return false;
        title=vcall<void*>(fm,FM_CREATE,"Deeps430Title"); if(!title) return false;
        init_font(title,true,0);
        for(uint32_t i=0;i<15;++i){ rows[i]=vcall<void*>(fm,FM_CREATE,kRowAliases[i]); if(rows[i]) init_font(rows[i],false,i); }
        update_positions(); render_meter(); return true;
    }
    void Direct3DBeginScene(bool) override {}
    void Direct3DEndScene(bool) override {}
    void Direct3DPresent(const RECT*, const RECT*, HWND, const RGNDATA*) override { if(visible) render_meter(); }
    bool Direct3DSetRenderState(D3DRENDERSTATETYPE, DWORD*) override { return false; }
    bool Direct3DDrawPrimitive(D3DPRIMITIVETYPE, UINT, UINT) override { return false; }
    bool Direct3DDrawIndexedPrimitive(D3DPRIMITIVETYPE, UINT, UINT, UINT, UINT) override { return false; }
    bool Direct3DDrawPrimitiveUP(D3DPRIMITIVETYPE, UINT, const void*, UINT) override { return false; }
    bool Direct3DDrawIndexedPrimitiveUP(D3DPRIMITIVETYPE, UINT, UINT, UINT, const void*, D3DFORMAT, const void*, UINT) override { return false; }

private:
    void clear_stats()
    {
        statPacket28=statDuplicate=statBadHeader=statNoEntityIndex=statNpcRejected=statEntityFull=0;
        statParsedActions=statRecordedActions=statUnknownMessage=statShortPacket=0;
        lastPacketSize=lastUserId=lastTargetCount=lastActionType=lastActionId=lastActionCount=0;
        lastEntityIndex=lastMessageId=lastDamage=0;
    }

    bool is_parsed_action_type(uint8_t actionType) const
    {
        return actionType==ACTIONTYPE_MELEE || actionType==ACTIONTYPE_RA_FINISH || actionType==ACTIONTYPE_WS_FINISH ||
               actionType==ACTIONTYPE_CAST_FINISH || actionType==ACTIONTYPE_JA || actionType==ACTIONTYPE_NPC_TP_FINISH ||
               actionType==ACTIONTYPE_AVATAR_BP_FINISH || actionType==ACTIONTYPE_JA_DNC || actionType==ACTIONTYPE_JA_RUN;
    }

    bool is_duplicate_packet(const uint8_t* data,uint32_t size)
    {
        if (size > MAX_PACKET_BYTES) return false;
        for (uint32_t i=0;i<MAX_PACKET_HISTORY;++i) {
            if (packetHistory[i].used && packetHistory[i].size==size && memcmp(packetHistory[i].data,data,size)==0)
                return true;
        }
        PacketHistory& slot=packetHistory[packetHistoryPos++ % MAX_PACKET_HISTORY];
        slot.used=true; slot.size=(uint16_t)size; memcpy(slot.data,data,size);
        return false;
    }

    void* entity_manager() const
    {
        if(!core) return 0;
        void* mm=vcall<void*>(core,CORE_MEMORY); if(!mm) return 0;
        return vcall<void*>(mm,MEMORY_ENTITY);
    }
    void* party_manager() const
    {
        if(!core) return 0;
        void* mm=vcall<void*>(core,CORE_MEMORY); if(!mm) return 0;
        return vcall<void*>(mm,MEMORY_PARTY);
    }

    uint16_t get_index_from_id(uint32_t id) const
    {
        void* em=entity_manager(); if(!em || !id) return 0;
        uint32_t mapSize=vcall<uint32_t>(em,ENTITY_MAP_SIZE);
        // Original Deeps scans 0x900. Current SDK exposes the map size; cap to the
        // historical 0x900 to avoid walking an unexpected interface result.
        if (mapSize==0 || mapSize>0x900u) mapSize=0x900u;
        for(uint32_t i=0;i<mapSize;++i)
            if(vcall<uint32_t>(em,ENTITY_SERVER_ID,i)==id) return (uint16_t)i;
        return 0;
    }

    int32_t find_party_index(uint32_t id) const
    {
        void* p=party_manager(); if(!p) return -1;
        for(uint32_t i=0;i<18;++i) if(vcall<uint32_t>(p,PARTY_SERVERID,i)==id) return (int32_t)i;
        return -1;
    }

    uint32_t color_for(uint32_t id) const
    {
        if(jobColors) {
            int32_t pidx=find_party_index(id);
            if(pidx>=0) {
                void* p=party_manager();
                if(p) { uint8_t job=vcall<uint8_t>(p,PARTY_MAINJOB,(uint32_t)pidx); if(job<24) return kJobColors[job]; }
            }
        }
        return kRandomColors[id % (sizeof(kRandomColors)/sizeof(kRandomColors[0]))];
    }

    EntitySources* find_entity(uint32_t id)
    {
        for(uint32_t i=0;i<MAX_ENTITIES;++i) if(entities[i].used && entities[i].id==id) return &entities[i];
        return 0;
    }

    EntitySources* create_entity(uint32_t id,uint32_t ownerid,uint16_t index)
    {
        EntitySources* existing=find_entity(id); if(existing) return existing;
        for(uint32_t i=0;i<MAX_ENTITIES;++i) if(!entities[i].used) {
            EntitySources* e=&entities[i]; memset(e,0,sizeof(*e)); e->used=true; e->id=id; e->ownerid=ownerid; e->color=color_for(ownerid?ownerid:id);
            void* em=entity_manager(); const char* n=0;
            if(em && index) n=vcall<const char*>(em,ENTITY_NAME,(uint32_t)index);
            copy_text(e->name,sizeof(e->name),(n&&*n)?n:"(Unknown)");
            return e;
        }
        return 0;
    }

    void register_pet_for_owner(EntitySources* owner,uint16_t ownerIndex,uint32_t ownerId)
    {
        if(!owner || owner->ownerid!=0) return;
        void* em=entity_manager(); if(!em) return;
        uint16_t petIndex=vcall<uint16_t>(em,ENTITY_PET_TARGET_INDEX,(uint32_t)ownerIndex);
        if(!petIndex) return;
        uint32_t petId=vcall<uint32_t>(em,ENTITY_SERVER_ID,(uint32_t)petIndex);
        if(!petId) return;
        EntitySources* pet=find_entity(petId);
        if(!pet) create_entity(petId,ownerId,petIndex);
        else pet->ownerid=ownerId;
    }

    DamageSource* find_source(EntitySources* e,uint32_t key)
    {
        if(!e) return 0;
        for(uint32_t i=0;i<MAX_SOURCES;++i) if(e->sources[i].used && e->sources[i].key==key) return &e->sources[i];
        return 0;
    }
    DamageSource* create_source(EntitySources* e,uint32_t key,const char* name,bool isMagic)
    {
        if(!e) return 0;
        DamageSource* old=find_source(e,key); if(old) return old;
        for(uint32_t i=0;i<MAX_SOURCES;++i) if(!e->sources[i].used) {
            DamageSource* s=&e->sources[i]; memset(s,0,sizeof(*s)); s->used=true; s->key=key; s->isMagic=isMagic; copy_text(s->name,sizeof(s->name),name); return s;
        }
        return 0;
    }

    DamageSource* get_special_source(EntitySources* e,uint32_t key,const char* name)
    {
        DamageSource* s=find_source(e,key); if(s) return s;
        return create_source(e,key,name,false);
    }

    DamageSource* get_damage_source(EntitySources* entityInfo,uint8_t actionType,uint16_t actionID,bool isPet)
    {
        uint32_t key = isPet ? 0x0BADC0DEu : (((uint32_t)actionID << 8) + actionType);
        DamageSource* old=find_source(entityInfo,key); if(old) return old;

        char name[48]; name[0]=0; bool magic=false;
        if(isPet) copy_text(name,sizeof(name),"Pet");
        else if(actionType==ACTIONTYPE_MELEE) copy_text(name,sizeof(name),"Attack");
        else if(actionType==ACTIONTYPE_RA_FINISH) copy_text(name,sizeof(name),"Ranged Attack");
        else {
            void* rm=core?vcall<void*>(core,CORE_RESOURCE):0;
            const char* resolved=0;
            if(rm && (actionType==ACTIONTYPE_WS_FINISH || actionType==ACTIONTYPE_NPC_TP_FINISH)) {
                IAbility* a=vcall<IAbility*>(rm,RES_ABILITY_ID,(uint32_t)actionID); if(a) resolved=a->Name[2];
            } else if(rm && actionType==ACTIONTYPE_CAST_FINISH) {
                ISpell* s=vcall<ISpell*>(rm,RES_SPELL_ID,(uint32_t)actionID); if(s) resolved=s->Name[2]; magic=true;
            } else if(rm && (actionType==ACTIONTYPE_JA || actionType==ACTIONTYPE_JA_DNC || actionType==ACTIONTYPE_JA_RUN)) {
                IAbility* a=vcall<IAbility*>(rm,RES_ABILITY_ID,(uint32_t)actionID+512u); if(a) resolved=a->Name[2];
            }
            if(resolved && *resolved) copy_text(name,sizeof(name),resolved);
            else if(actionType==ACTIONTYPE_WS_FINISH || actionType==ACTIONTYPE_NPC_TP_FINISH) append_action_id(name,sizeof(name),"WS",actionID);
            else if(actionType==ACTIONTYPE_CAST_FINISH) append_action_id(name,sizeof(name),"Spell",actionID);
            else if(actionType==ACTIONTYPE_JA || actionType==ACTIONTYPE_JA_DNC || actionType==ACTIONTYPE_JA_RUN) append_action_id(name,sizeof(name),"Ability",actionID);
            else if(actionType==ACTIONTYPE_AVATAR_BP_FINISH) copy_text(name,sizeof(name),"Pet");
            else append_action_id(name,sizeof(name),"Action",actionID);
        }
        return create_source(entityInfo,key,name,magic);
    }

    static void add_damage_stat(DamageStat* st,uint32_t damage)
    {
        if(!st) return;
        st->total += damage;
        st->count++;
        if(st->count==1 || damage<st->min) st->min=damage;
        if(damage>st->max) st->max=damage;
    }

    bool update_damage_source(DamageSource* source,uint16_t message,uint32_t damage)
    {
        if(!source) return false;
        DamageKind kind;
        bool damaging=false;
        if(has_msg(kHitMessages,sizeof(kHitMessages)/sizeof(kHitMessages[0]),message)) { kind=DK_HIT; damaging=true; }
        else if(has_msg(kCritMessages,sizeof(kCritMessages)/sizeof(kCritMessages[0]),message)) { kind=DK_CRIT; damaging=true; }
        else if(has_msg(kMissMessages,sizeof(kMissMessages)/sizeof(kMissMessages[0]),message)) kind=DK_MISS;
        else if(has_msg(kEvadeMessages,sizeof(kEvadeMessages)/sizeof(kEvadeMessages[0]),message)) kind=DK_EVADE;
        else if(has_msg(kParryMessages,sizeof(kParryMessages)/sizeof(kParryMessages[0]),message)) kind=DK_PARRY;
        else return false;
        add_damage_stat(&source->damage[kind],damaging?damage:0);
        return true;
    }

    uint64_t source_total(const DamageSource* s) const
    {
        uint64_t t=0; if(!s||!s->used)return 0; for(uint32_t k=0;k<DK_COUNT;++k)t+=s->damage[k].total; return t;
    }
    uint64_t source_count(const DamageSource* s) const
    {
        uint64_t t=0; if(!s||!s->used)return 0; for(uint32_t k=0;k<DK_COUNT;++k)t+=s->damage[k].count; return t;
    }
    uint64_t entity_total(const EntitySources* e) const
    {
        uint64_t t=0; if(!e||!e->used)return 0; for(uint32_t i=0;i<MAX_SOURCES;++i)if(e->sources[i].used)t+=source_total(&e->sources[i]); return t;
    }
    uint32_t entity_hitrate10(const EntitySources* e) const
    {
        if(!e)return 0; uint64_t totalCount=0,totalMiss=0;
        for(uint32_t i=0;i<MAX_SOURCES;++i) {
            const DamageSource* s=&e->sources[i]; if(!s->used||s->isMagic||streq(s->name,"Skillchain")||streq(s->name,"Pet"))continue;
            totalCount+=source_count(s); totalMiss+=s->damage[DK_MISS].count;
        }
        return totalCount? (uint32_t)u64_div((totalCount-totalMiss)*1000ULL,totalCount):0;
    }

    void reset_damage()
    {
        memset(entities,0,sizeof(entities)); memset(packetHistory,0,sizeof(packetHistory)); packetHistoryPos=0; clear_stats();
    }

    void init_font(void* f,bool isTitle,uint32_t)
    {
        if(!f)return; float scale=tvMode?1.5f:1.0f;
        vcall<void>(f,FONT_SET_FONTFAMILY,"Arial"); vcall<void>(f,FONT_SET_FONTHEIGHT,(uint32_t)((isTitle?10:8)*scale));
        vcall<void>(f,FONT_SET_AUTORESIZE,false); vcall<void>(f,FONT_SET_CANFOCUS,false); vcall<void>(f,FONT_SET_LOCKED,true);
        vcall<void>(f,FONT_SET_COLOR,0xFFFFFFFFu); vcall<void>(f,FONT_SET_COLOR_OUTLINE,0xFF000000u); vcall<void>(f,FONT_SET_DRAWFLAGS,(uint32_t)16u);
        vcall<void>(f,FONT_SET_VISIBLE,visible);
        void* bg=vcall<void*>(f,FONT_GET_BACKGROUND); if(bg){ vcall<void>(bg,PRIM_SET_VISIBLE,true);vcall<void>(bg,PRIM_SET_BORDER_VISIBLE,false);vcall<void>(bg,PRIM_SET_CANFOCUS,false);vcall<void>(bg,PRIM_SET_COLOR,isTitle?0xCC000000u:0xFF007C5Cu);vcall<void>(bg,PRIM_SET_WIDTH,(isTitle?258.0f:250.0f)*scale);vcall<void>(bg,PRIM_SET_HEIGHT,(isTitle?17.0f:13.0f)*scale); }
        if(isTitle)vcall<void>(f,FONT_SET_TEXT," Deeps - Damage Done"); else vcall<void>(f,FONT_SET_TEXT,"");
    }
    void update_positions()
    {
        float scale=tvMode?1.5f:1.0f;
        if(title){vcall<void>(title,FONT_SET_POSX,xpos);vcall<void>(title,FONT_SET_POSY,ypos);}
        for(uint32_t i=0;i<15;++i)if(rows[i]){vcall<void>(rows[i],FONT_SET_POSX,xpos+4.0f*scale);vcall<void>(rows[i],FONT_SET_POSY,ypos+(16.0f+(float)i*16.0f)*scale);}
    }
    void set_all_visible(bool v)
    {
        if(title)vcall<void>(title,FONT_SET_VISIBLE,v); for(uint32_t i=0;i<15;++i)if(rows[i])vcall<void>(rows[i],FONT_SET_VISIBLE,v);
    }

    uint32_t collect_sorted(uint32_t* idx)
    {
        uint32_t n=0;
        for(uint32_t i=0;i<MAX_ENTITIES&&n<MAX_ENTITIES;++i){
            if(!entities[i].used||entities[i].ownerid!=0||entity_total(&entities[i])==0)continue;
            if(partyOnly&&find_party_index(entities[i].id)<0)continue;
            idx[n++]=i;
        }
        for(uint32_t a=0;a<n;++a)for(uint32_t b=a+1;b<n;++b)if(entity_total(&entities[idx[b]])>entity_total(&entities[idx[a]])){uint32_t t=idx[a];idx[a]=idx[b];idx[b]=t;}
        return n;
    }

    void render_meter()
    {
        if(!title)return; float scale=tvMode?1.5f:1.0f;
        vcall<void>(title,FONT_SET_FONTHEIGHT,(uint32_t)(10.0f*scale));
        void* tbg=vcall<void*>(title,FONT_GET_BACKGROUND);if(tbg){vcall<void>(tbg,PRIM_SET_WIDTH,258.0f*scale);vcall<void>(tbg,PRIM_SET_HEIGHT,17.0f*scale);}
        update_positions();
        uint32_t idx[MAX_ENTITIES];uint32_t n=collect_sorted(idx);if(n>15)n=15;
        uint64_t total=0,max=0;for(uint32_t i=0;i<n;++i){uint64_t et=entity_total(&entities[idx[i]]);total+=et;if(et>max)max=et;}
        for(uint32_t i=0;i<15;++i){
            void* f=rows[i];if(!f)continue;if(i>=n){vcall<void>(f,FONT_SET_VISIBLE,false);continue;}
            EntitySources& e=entities[idx[i]]; uint64_t et=entity_total(&e);
            vcall<void>(f,FONT_SET_VISIBLE,visible);vcall<void>(f,FONT_SET_FONTHEIGHT,(uint32_t)(8.0f*scale));
            char buf[192];uint32_t p=0;buf[0]=0;
            p=append_str(buf,p,sizeof(buf)," ");p=append_u32(buf,p,sizeof(buf),i+1);p=append_str(buf,p,sizeof(buf),". ");p=append_pad_name(buf,p,sizeof(buf),e.name,10);p=append_str(buf,p,sizeof(buf)," ");p=append_u64(buf,p,sizeof(buf),et);p=append_str(buf,p,sizeof(buf)," (");
            uint32_t pct=total?(uint32_t)u64_div(et*1000ULL,total):0;p=append_pct10(buf,p,sizeof(buf),pct);p=append_str(buf,p,sizeof(buf),")  -  Hit: ");p=append_pct10(buf,p,sizeof(buf),entity_hitrate10(&e));
            vcall<void>(f,FONT_SET_TEXT,buf);
            void* bg=vcall<void*>(f,FONT_GET_BACKGROUND);if(bg){float w=max?(250.0f*scale*((float)et/(float)max)):250.0f*scale;vcall<void>(bg,PRIM_SET_WIDTH,w);vcall<void>(bg,PRIM_SET_HEIGHT,13.0f*scale);vcall<void>(bg,PRIM_SET_COLOR,color_for(e.id));vcall<void>(bg,PRIM_SET_VISIBLE,true);}
        }
    }

    void write_chat(const char* s) const
    {
        if(!core||!s)return;void* c=vcall<void*>(core,CORE_CHAT);if(c)vcall<void>(c,CHAT_WRITE,0,false,s);
    }
    void print_help()
    {
        write_chat("[Deeps] /dps reset | report | status | debug | partyonly | jobcolors | sc | tvmode | pos X Y | hide | show");
    }
    void report_local()
    {
        uint32_t idx[MAX_ENTITIES];uint32_t n=collect_sorted(idx);if(n>10)n=10;
        if(n==0){write_chat("[Deeps] No damage recorded.");return;}write_chat("[Deeps] Damage Done:");
        for(uint32_t i=0;i<n;++i){char b[128];uint32_t p=0;b[0]=0;p=append_str(b,p,sizeof(b),"  ");p=append_u32(b,p,sizeof(b),i+1);p=append_str(b,p,sizeof(b),". ");p=append_str(b,p,sizeof(b),entities[idx[i]].name);p=append_str(b,p,sizeof(b)," - ");p=append_u64(b,p,sizeof(b),entity_total(&entities[idx[i]]));write_chat(b);}
    }
    void print_status()
    {
        char b[220];uint32_t p=0;b[0]=0;
        p=append_str(b,p,sizeof(b),"[Deeps] 0x28=");p=append_u32(b,p,sizeof(b),statPacket28);
        p=append_str(b,p,sizeof(b)," parsed=");p=append_u32(b,p,sizeof(b),statParsedActions);
        p=append_str(b,p,sizeof(b)," recorded=");p=append_u32(b,p,sizeof(b),statRecordedActions);
        p=append_str(b,p,sizeof(b)," noEntity=");p=append_u32(b,p,sizeof(b),statNoEntityIndex);
        p=append_str(b,p,sizeof(b)," badHeader=");p=append_u32(b,p,sizeof(b),statBadHeader);
        write_chat(b);
        p=0;b[0]=0;
        p=append_str(b,p,sizeof(b),"[Deeps] short=");p=append_u32(b,p,sizeof(b),statShortPacket);
        p=append_str(b,p,sizeof(b)," duplicate=");p=append_u32(b,p,sizeof(b),statDuplicate);
        p=append_str(b,p,sizeof(b)," npc=");p=append_u32(b,p,sizeof(b),statNpcRejected);
        p=append_str(b,p,sizeof(b)," unknownMsg=");p=append_u32(b,p,sizeof(b),statUnknownMessage);
        write_chat(b);
        p=0;b[0]=0;
        p=append_str(b,p,sizeof(b),"[Deeps] last size=");p=append_u32(b,p,sizeof(b),lastPacketSize);
        p=append_str(b,p,sizeof(b)," actor=");p=append_u32(b,p,sizeof(b),lastUserId);
        p=append_str(b,p,sizeof(b)," idx=");p=append_u32(b,p,sizeof(b),lastEntityIndex);
        p=append_str(b,p,sizeof(b)," tgt=");p=append_u32(b,p,sizeof(b),lastTargetCount);
        p=append_str(b,p,sizeof(b)," type=");p=append_u32(b,p,sizeof(b),lastActionType);
        p=append_str(b,p,sizeof(b)," id=");p=append_u32(b,p,sizeof(b),lastActionId);
        p=append_str(b,p,sizeof(b)," n=");p=append_u32(b,p,sizeof(b),lastActionCount);
        write_chat(b);
        p=0;b[0]=0;
        p=append_str(b,p,sizeof(b),"[Deeps] last message=");p=append_u32(b,p,sizeof(b),lastMessageId);
        p=append_str(b,p,sizeof(b)," damage=");p=append_u32(b,p,sizeof(b),lastDamage);
        write_chat(b);
    }

    void load_config()
    {
        void* cfg=core?vcall<void*>(core,CORE_CONFIG):0;if(!cfg)return;
        xpos=vcall<float>(cfg,CFG_GETFLOAT,"Deeps","guipos","xpos",300.0f);ypos=vcall<float>(cfg,CFG_GETFLOAT,"Deeps","guipos","ypos",300.0f);
        tvMode=vcall<bool>(cfg,CFG_GETBOOL,"Deeps","tvmode","enabled",false);countSkillchains=vcall<bool>(cfg,CFG_GETBOOL,"Deeps","sc","enabled",true);
        partyOnly=vcall<bool>(cfg,CFG_GETBOOL,"Deeps","partyonly","enabled",true);jobColors=vcall<bool>(cfg,CFG_GETBOOL,"Deeps","jobcolors","enabled",true);
    }
    void save_config()
    {
        void* cfg=core?vcall<void*>(core,CORE_CONFIG):0;if(!cfg)return;char xb[24],yb[24];i32_to_text(xb,sizeof(xb),(int32_t)xpos);i32_to_text(yb,sizeof(yb),(int32_t)ypos);
        vcall<void>(cfg,CFG_SETVALUE,"Deeps","guipos","xpos",xb);vcall<void>(cfg,CFG_SETVALUE,"Deeps","guipos","ypos",yb);
        vcall<void>(cfg,CFG_SETVALUE,"Deeps","tvmode","enabled",tvMode?"1":"0");vcall<void>(cfg,CFG_SETVALUE,"Deeps","sc","enabled",countSkillchains?"1":"0");
        vcall<void>(cfg,CFG_SETVALUE,"Deeps","partyonly","enabled",partyOnly?"1":"0");vcall<void>(cfg,CFG_SETVALUE,"Deeps","jobcolors","enabled",jobColors?"1":"0");vcall<bool>(cfg,CFG_SAVE,"Deeps","Deeps");
    }
    static void i32_to_text(char* out,uint32_t cap,int32_t v)
    {
        uint32_t p=0;out[0]=0;uint32_t mag;if(v<0){if(p+1<cap)out[p++]='-';mag=(uint32_t)(-(v+1))+1u;}else mag=(uint32_t)v;append_u32(out,p,cap,mag);
    }
};

alignas(16) static uint8_t g_storage[sizeof(Deeps430)];
static Deeps430* g_instance = 0;

extern "C" double __stdcall expGetInterfaceVersion(void) { return 4.30; }
extern "C" IPlugin* __stdcall expCreatePlugin(const char*)
{
    if (g_instance) return g_instance;
    g_instance = new ((void*)g_storage) Deeps430();
    return g_instance;
}
extern "C" void __stdcall expDestroyPlugin(void* instance)
{
    if (instance && instance == (void*)g_instance) {
        g_instance->~Deeps430();
        g_instance = 0;
    }
}
