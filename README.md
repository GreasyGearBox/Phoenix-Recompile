Deeps 1.06 core port for Ashita 4.30 / Phoenix XI
=================================================

IMPORTANT
---------
This is an unofficial Ashita 4.30 compatibility port based on relliko's Deeps
1.06 source. It is not an upstream relliko release. The core damage-event path
was ported from Deeps rather than recreated as a UI-only shell.

Upstream Deeps:
https://github.com/relliko/Deeps

Target:
- Ashita plugin interface 4.30
- 32-bit x86 / PE32
- Phoenix XI


RECOMPILE / COMPATIBILITY SUMMARY
---------------------------------
Deeps 1.06 was retargeted to Phoenix’s **Ashita 4.30 plugin ABI** and rebuilt as a **32-bit x86 Windows DLL**. I updated the required Ashita interface methods and exports (`expCreatePlugin`, `expDestroyPlugin`, and `expGetInterfaceVersion`) so Ashita 4.30 would recognize and load it correctly.

The main runtime fix was the combat parser. The old port assumed incoming `0x028` action packets had a fixed minimum size of roughly **46 bytes**, causing normal ~35-byte melee packets to be rejected. I replaced that with **bit-based, variable-length packet parsing** with proper bounds checking, allowing normal attacks, multi-hit actions, multiple targets, additional effects, spikes, skillchains, WS/spells, and similar actions to parse correctly.

I also changed actor resolution from relying primarily on party slots to using the **full FFXI entity map**, restored **pet-to-owner attribution**, and added `/dps status` diagnostics so we could verify whether packets were reaching the plugin, parsing correctly, resolving entities, and actually recording damage.

The final DLL was compiled specifically as **PE32/x86 for Ashita 4.30**, with the required exports and no external CRT/runtime dependency.

The especially important distinction is: **the original source needed compatibility/code changes before recompiling; it wasn't just a compiler-setting change.**

INSTALL
-------
1. Back up your current Deeps.dll.
2. Put this Deeps.dll in the exact same Phoenix plugin folder as the current one.
3. If Deeps is not already auto-loaded, use:
      /load deeps

FIRST TEST -- PLEASE DO THIS BEFORE ANYTHING ELSE
-------------------------------------------------
1. Attack a normal mob with a melee weapon for 2-3 successful swings.
2. Run:
      /dps status

Expected healthy core capture:
- 0x28 > 0
- parsed > 0
- recorded > 0
- last type=1 for ordinary melee
- last damage > 0 on a successful hit

The status counters deliberately identify the exact failure stage:
- 0x28=0                : Ashita did not invoke the packet callback.
- noEntity increasing   : actor ID was seen but entity lookup failed.
- badHeader increasing  : action header/category/ID validation failed.
- short increasing      : variable-length action cursor ran out of packet bits.
- unknownMsg increasing : packet parsed, but the message ID is not classified.
- parsed>0 recorded>0   : combat capture is working; any remaining problem is UI/accounting.

CORE PORTED BEHAVIOR
--------------------
- Incoming 0x028 action packet handling.
- Variable-length action parsing; no fixed 46-byte minimum.
- Full entity-map actor lookup rather than party-slot-only lookup.
- Pet registration and owner attribution.
- Melee, ranged, WS, spell, JA, NPC TP/BP action categories.
- Hit, crit, miss, evade and parry accounting.
- Daken handling.
- Additional-effect and skillchain damage accounting.
- Multi-action and multi-target cursor handling.
- 200-packet duplicate history.
- 64-bit damage totals.

DIAGNOSTIC / UTILITY COMMANDS
-----------------------------
/dps status
/dps reset
/dps report
/dps debug
/dps partyonly
/dps jobcolors
/dps sc
/dps tvmode
/dps pos X Y
/dps hide
/dps show

NOT YET PORTED FROM THE ORIGINAL UI
-----------------------------------
- Left-click per-source drilldown.
- Right-click back navigation.
- Shift-drag window repositioning. Use /dps pos X Y instead.
- Original delayed chat-channel reporting behavior; /dps report prints locally.

VALIDATION PERFORMED BEFORE PACKAGING
-------------------------------------
- Compiled as PE32/x86 for Windows.
- Ashita interface export is 4.30.
- Required exports present: expCreatePlugin, expDestroyPlugin,
  expGetInterfaceVersion.
- No external CRT/runtime DLL imports.
- Compiler warning pass completed cleanly.
- Synthetic 0x028 parser tests passed for:
    * 35-byte one-hit melee packet with Param=0
    * two actions on one target
    * optional additional-effect body followed by another hit
    * optional spikes body followed by another hit
    * two targets with different action counts

A real Phoenix/FFXI process cannot be launched in the build environment, so the
/dps status test above is the required live validation of packet delivery and
entity resolution.
