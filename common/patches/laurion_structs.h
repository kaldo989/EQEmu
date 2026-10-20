#ifndef LAURION_STRUCTS_H_
#define LAURION_STRUCTS_H_

#include "laurion_limits.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace Laurion {
		// EqGuid is a 8-byte GUID used for character/world identification
		struct EqGuid {
			uint32 Id;
			uint16 WorldId;
			uint16 Padding;
		};

	namespace structs {
		// constants
		static const uint32 MAX_PP_AA_ARRAY = 300;
		static const uint32 MAX_PP_SKILL = PACKET_SKILL_ARRAY_SIZE;
		static const uint32 MAX_PP_INNATE_SKILL = 25;
		static const uint32 MAX_PP_DISCIPLINES = 300;
		static const uint32 MAX_PP_COMBAT_ABILITY_TIMERS = 25;
		static const uint32 MAX_PP_UNKNOWN_ABILITIES = 25;
		static const uint32 MAX_RECAST_TYPES = 25;
		static const uint32 MAX_ITEM_RECAST_TYPES = 100;
		static const uint32 BLOCKED_BUFF_COUNT = 40;
		static const uint32 BUFF_COUNT = 62;
		static const uint32 MAX_PP_LANGUAGE = 32;
#pragma pack(1)
		
		struct LoginInfo_Struct {
			/*000*/	char	login_info[64];
			/*064*/	uint8	unknown064[124];
			/*188*/	uint8	zoning;			// 01 if zoning, 00 if not
			/*189*/	uint8	unknown189[275];
			/*488*/
		};

		struct ClientZoneEntry_Struct {
			/*00*/ uint32	unknown00;	// ***Placeholder
			/*04*/ char	char_name[64];	// Player firstname [32]
			/*68*/ uint32	unknown68;
			/*72*/ uint32	unknown72;
			/*76*/ uint32	unknown76;
			/*80*/ uint32	unknown80;
			/*84*/ uint32	unknown84;
			/*88*/ uint32	unknown88;
			/*92*/
		};

		struct Membership_Struct
		{
			/*000*/ uint8 membership; //0 not gold, 2 gold
			/*001*/ uint32 races;	// Seen ff ff 01 00
			/*005*/ uint32 classes;	// Seen ff ff 01 00
			/*009*/ uint32 entrysize; // Seen 33
			/*013*/ int32 entries[33]; // FreeToPlayClient::LimitData[0..32], see docs/op_2aca_membership.md
			/*145*/
		};

		struct Membership_Entry_Struct
		{
			/*000*/ uint32 purchase_id;		// Seen 1, then increments 90287 to 90300
			/*004*/ uint32 bitwise_entry;	// Seen 16 to 65536 - Skips 4096
			/*008*/
		};

		struct Membership_Setting_Struct
		{
			/*000*/ int8 setting_index;	// 0, 1, 2 or 3: f2p, silver, gold, platinum?
			/*001*/ int32 setting_id;		// 0 to 23 actually seen but the OP_Membership packet has up to 32
			/*005*/ int32 setting_value;	
			/*009*/
		};

		struct Membership_Details_Struct
		{
			/*000*/ uint32 membership_setting_count;	// Seen 96
			/*004*/ Membership_Setting_Struct settings[96]; // 864 Bytes
			/*364*/ uint32 race_entry_count;	// Seen 17
			/*368*/ Membership_Entry_Struct membership_races[17]; // 136 Bytes
			/*3f0*/ uint32 class_entry_count;	// Seen 15
			/*3f4*/ Membership_Entry_Struct membership_classes[17]; // 136 Bytes
			/*47c*/ uint32 exit_url_length;	// Length of the exit_url string (0 for none)
			/*480*/ //char exit_url[42];		// Upgrade to Silver or Gold Membership URL
		};

		struct MaxCharacters_Struct {
			/*000*/ uint32 max_chars;
			/*004*/ uint32 marketplace_chars;
			/*008*/ int32 unknown008; //some of these probably deal with heroic characters or something
			/*00c*/ int32 unknown00c;
			/*010*/ int32 unknown010;
			/*014*/ int32 unknown014;
			/*018*/ int32 unknown018;
			/*01c*/ int32 unknown01c;
			/*020*/ int32 unknown020;
			/*024*/ int32 unknown024;
			/*028*/ int32 unknown028;
			/*02c*/ int32 unknown02c;
			/*030*/ int32 unknown030;
			/*034*/ int32 unknown034;
			/*038*/
		};

		struct ExpansionInfo_Struct {
			/*000*/	char	Unknown000[64];
			/*064*/	uint32	Expansions;
		};

		/*
		** 0x6d4d - World/RP server info.
		**
		** The client handler is the inline WorldAuthenticate case body at 0x1402a78a0. It reads a
		** set of fixed-offset fields and copies a null-terminated server name from payload +0x15
		** into DAT_140e3d630. The handler indexes up to payload +0x71F, so the payload must be at
		** least 0x720 bytes. Only the server name offset is verified against the disassembly
		** (LEA RDX,[RBX+0x15]); the remaining fields have no known semantics and are sent as zeros.
		**
		** The generic EQEmu LogServer_Struct is 279 bytes with worldshortname at +0x20 - both the
		** size and the name offset are wrong for this client.
		*/
		static const size_t LAURION_LOGSERVER_SIZE = 0x720;
		static const size_t LAURION_LOGSERVER_NAME_OFFSET = 0x15;

		/*
		** 0x2aca - FreeToPlay membership.
		**
		** Wire order verified from FUN_140640960 (bounds-guarded stream reader), the applier called by
		** the 0x2aca handler at 0x1401df153 and by the WorldAuthenticate handler FUN_1402a6fd0 at
		** 0x1402a7719:
		**   [uint8 flag]                    -> FreeToPlayClient +0x00 (MembershipLevel)
		**   [uint32]                        -> FreeToPlayClient +0x04 (Races bitmask, tested by the 0x832 handler)
		**   [uint32]                        -> FreeToPlayClient +0x08 (Classes bitmask, tested by the 0x832 handler)
		**   [uint32 count][count x uint32]  -> FreeToPlayClient +0x0C + i*4 (LimitData), i < 33
		** The loop guard is `if (0x20 < i) return;`, so index 32 is the last entry the client stores.
		** Every read is bounds-checked against the remaining payload, so a short packet leaves the
		** untouched LimitData entries at their previous value instead of zeroing them.
		** The generic EQEmu Membership_Struct has no leading byte flag, so every field sits one
		** byte earlier than the client expects.
		*/
		struct MembershipWireHead_Struct {
			/*000*/	uint8  Flag;
			/*001*/	uint32  ClassBitmask;
			/*005*/	uint32  RaceBitmask;
			/*009*/	uint32  EntryCount;
		};

		/*
		** 0x2608 - FreeToPlay settings. FUN_1406407b0 CLEARS the FreeToPlay object (including the
		** +0x04/+0x08 bitmasks) before filling its tables, so this packet must be sent BEFORE
		** 0x2aca or the bitmasks are wiped before the 0x832 character list is parsed.
		*/
		static const size_t LAURION_PING_SIZE = 20;	// 0x2049: handler reads dwords 0, 2 and 4

		/*
		* Visible equiptment.
		* Size: 20 Octets
		*/
		struct Texture_Struct
		{
			uint32 Material;
			uint32 Unknown1;
			uint32 EliteMaterial;
			uint32 HeroForgeModel;
			uint32 Material2;	// Same as material?
		};

		/*
		** Color_Struct
		** Size: 4 bytes
		** Used for convenience
		** Merth: Gave struct a name so gcc 2.96 would compile
		**
		*/
		struct Tint_Struct
		{
			union {
				struct {
					uint8 Blue;
					uint8 Green;
					uint8 Red;
					uint8 UseTint;	// if there's a tint this is FF
				};
				uint32 Color;
			};
		};

		struct CharSelectEquip : Texture_Struct, Tint_Struct {};

		/*
		** 0x832 character entry - WIRE layout, verified against the client parser
		** (FUN_1401f0d90, called from the 0x832 handler FUN_140200f40).
		**
		** The parser is a bounds-checked stream reader, so entries are VARIABLE length:
		**   [null-terminated name][23 byte head][9 x 24 byte armor/tint][52 byte tail]
		**   = 292 + strlen(name)   (355 only when the name is exactly 63 chars)
		**
		** The client stores each entry at a 0x170 (368) byte stride in memory - that is
		** storage, not the wire size. Wire order is NOT the same as memory order for the
		** armor block (permuted) or the tail (memory gaps at 0x135/0x136/0x161).
		**
		** Memory layout reference: MacroQuestLS EverQuest.h CharSelectInfo (0x170 bytes).
		*/
		struct CharSelectArmorSlot_Struct {	// 24 bytes on the wire, in wire order
			uint32 ArmorType;		// ArmorProperties dword 0
			uint32 Material;		// ArmorProperties dword 2
			uint32 Variation;		// ArmorProperties dword 1
			uint32 NewArmorID;		// ArmorProperties dword 3
			uint32 NewArmorType;	// ArmorProperties dword 4
			uint32 Tint;			// Tint[9] dword
		};

		struct CharSelectEntryHead_Struct {	// 23 bytes, in wire order (follows the name string)
			uint32 Class;			// memory +0x40
			uint32 Race;			// memory +0x44
			uint8  Level;			// memory +0x48
			uint32 ShroudClass;		// memory +0x4C
			uint32 ShroudRace;		// memory +0x50
			uint32 CurZoneID;		// memory +0x54
			uint8  Sex;			// memory +0x55
			uint8  Face;			// memory +0x56
		};

		struct CharSelectEntryTail_Struct {	// 52 bytes, in wire order
			uint8  TextureType;		// memory +0x134
			uint8  HeadType;		// memory +0x137
			uint32 TattooIndex;		// memory +0x138
			uint32 FacialAttachmentIndex;	// memory +0x13C
			uint32 Deity;			// memory +0x140
			uint32 PrimActor;		// memory +0x144
			uint32 SecdActor;		// memory +0x148
			uint8  HairColor;		// memory +0x14C
			uint8  BeardColor;		// memory +0x14D
			uint8  LeftEye;		// memory +0x14E
			uint8  RightEye;		// memory +0x14F
			uint8  Hair;			// memory +0x150
			uint8  Beard;			// memory +0x151
			uint8  bCanGoHome;		// memory +0x152
			uint8  bCanTutorial;		// memory +0x153
			uint32 ParentId;		// memory +0x154
			uint8  bTooHighLevel;	// memory +0x158
			uint8  bPreFTP;			// memory +0x159
			uint32 SomethingLogin;	// memory +0x15C
			uint8  bUseable;		// memory +0x160
			uint16 bShrouded;		// memory +0x162
			uint8  Unknown0x164;		// memory +0x164
			uint8  TailBlock[8];		// memory +0x168 (read by FUN_1401d4ab0)
		};

		/*
		** Character Selection Struct (0x832)
		**
		** Wire: [uint32 CharCount][N x variable-length entries][uint8 trailing flag -> DAT_140e3bee4]
		** There is no TotalChars field on the wire for this client - the generic emu struct has one,
		** but the client parser reads the count and then goes straight into the entries.
		*/
		struct CharacterSelect_Struct
		{
			/*000*/	uint32 CharCount;	//number of chars in this packet
		};

		enum LaurionAppearance : uint32
		{
			None,
			WhoLevel,
			MaxHealth,
			Invisibility,
			PVP,
			Light,
			Animation,
			Sneak,
			SpawnID,
			Health,
			Linkdead,
			FlyMode,
			GM,
			Anonymous,
			// Field 14 is the guild RANK, not the guild id. FUN_140201ce0 case 0xe compares the
			// incoming value with the member's stored rank and prints "You are now the rank of %s"
			// (string 0x1de7 for the local player, 0x1de8 for another member). The spawn's guild
			// GUID at PlayerClient + 0x208 is written by the guild packets, never by an appearance
			// packet, so EQEmu's AppearanceType::GuildID has no Laurion field.
			GuildRank,
			AFK,
			Pet,
			Summoned,
			Unknown18,
			Unknown19,
			SetType,
			NPCName,
			CancelSneakHide,
			AreaHealthRegen,
			AreaManaRegen,
			AreaEnduranceRegen,
			FreezeBeneficialBuffs,
			NPCTintIndex,
			Unknown28,
			Unknown29,
			Unknown30,
			ShowHelm,
			DamageState,
			Unknown33, //Some virtual function call; based on location might be EQPlayers (my guess personally) or FindBits
			TextureType, //Texture ID
			Unknown35,
			Unknown36,
			GuildShow,
			OfflineMode,
			Unknown39,
			Unknown40,
			Unknown41,
			Unknown42,
			Birthdate,
			EncounterLock
		};

		struct SpawnAppearance_Struct
		{
			/*0000*/ uint32 spawn_id;		// ID of the spawn
			/*0004*/ uint32 type;			// Values associated with the type
			/*0008*/ uint64 parameter;		// Type of data sent
			/*0016*/ uint64 lock_id; //the only place client uses this as far as I can tell is when you send 0x2c as type in which case it sets LockID = this
			/*0024*/
		};

		struct Spawn_Struct_Bitfields
		{
			union {
				struct {
					// byte 1
					/*00*/	unsigned   gender : 2;		// Gender (0=male, 1=female, 2=monster)
					/*02*/	unsigned   ispet : 1;			// Guessed based on observing live spawns
					/*03*/	unsigned   afk : 1;			// 0=no, 1=afk
					/*04*/	unsigned   anon : 2;			// 0=normal, 1=anon, 2=roleplay
					/*06*/	unsigned   gm : 1;
					/*07*/	unsigned   sneak : 1;
					// byte 2
					/*08*/	unsigned   lfg : 1;
					/*09*/	unsigned   unk9 : 1;
					/*10*/	unsigned   invis : 12;		// there are 3000 different (non-GM) invis levels
					/*22*/	unsigned   linkdead : 1;		// 1 Toggles LD on or off after name. Correct for RoF2
					/*23*/	unsigned   showhelm : 1;
					// byte 4
					/*24*/	unsigned   betabuffed : 1;		// Prefixes name with !
					/*25*/	unsigned   trader : 1;
					/*26*/	unsigned   animationonpop : 1;
					/*27*/	unsigned   targetable : 1;
					/*28*/	unsigned   targetable_with_hotkey : 1;
					/*29*/	unsigned   showname : 1;
					/*30*/	unsigned   idleanimationsoff : 1; // what we called statue?
					/*31*/	unsigned   untargetable : 1;	// bClickThrough
					// byte 5
					/*32*/	unsigned   buyer : 1;
					/*33*/	unsigned   offline : 1;
					/*34*/	unsigned   interactiveobject : 1;
					/*35*/	unsigned   missile : 1;
					/*36*/	unsigned   title : 1;
					/*37*/	unsigned   suffix : 1;
					/*38*/	unsigned   unk38 : 1;
					/*39*/	unsigned   unk39 : 1;
				};
				uint8 raw[5];
			};
		};

		struct Spawn_Struct_Position
		{
			union {
				struct {
					signed y : 19;
					signed deltaX : 13;

					unsigned heading : 12;
					signed z : 19;
					unsigned pad1 : 1;

					unsigned pitch : 12;
					signed animation : 10;    //these might be swapped
					signed deltaHeading : 10; //these might be swapped

					signed deltaY : 13;
					signed deltaZ : 13;
					unsigned pad3 : 6;

					signed x : 19;
					unsigned pad4 : 13;
				};
				uint32_t raw[5];
			};
		};

		struct Client_Position
		{
			/*00*/ float delta_x;
			/*04*/ float x;
			/*08*/ float z;
			/*12*/ signed animation : 10;
			       unsigned pitch : 12;
			       signed padding1 : 10;
			/*16*/ float delta_y;
			/*20*/ float y;
			/*24*/ signed delta_heading : 10;
				   signed heading : 12;
				   signed padding2 : 10;
			/*28*/ float delta_z;
			/*32*/ 
		};

		struct PlayerPositionUpdateServer_Struct
		{
			/*00*/ uint16 spawn_id;
			/*02*/ uint16 vehicle_id;
			/*04*/ Spawn_Struct_Position position;
			/*24*/
		};

		struct PlayerPositionUpdateClient_Struct {
			/*00*/ uint16 sequence;
			/*02*/ uint16 spawn_id;
			/*04*/ uint16 vehicle_id;
			/*06*/ Client_Position position;
			/*38*/
		};

		struct Door_Struct
		{
			/*000*/ char name[32];
			/*032*/ float DefaultY;
			/*036*/ float DefaultX;
			/*040*/ float DefaultZ;
			/*044*/ float DefaultHeading;
			/*048*/ uint32 DefaultDoorAngle; //rof2's incline
			/*052*/ float Y; //most (all I've seen?) doors match the defaults here
			/*056*/ float X;
			/*060*/ float Z;
			/*064*/ float Heading;
			/*068*/ float DoorAngle; //not sure if this is actually a float; it might be a uint32 like DefaultDoorAngle
			/*072*/ uint32 ScaleFactor; //rof2's size
			/*076*/ uint32 Unknown76; //client doesn't seem to read this
			/*080*/ uint8 Id; //doorid
			/*081*/ uint8 Type; //opentype
			/*082*/ uint8 State; //state_at_spawn
			/*083*/ uint8 DefaultState; //invert_state
			/*084*/ int32 Param; //door_param
			/*088*/ uint32 AdventureDoorId;
			/*092*/ uint32 DynDoorID;
			/*096*/ uint32 RealEstateDoorID;
			/*100*/ uint8 bHasScript;
			/*101*/ uint8 bUsable; //1 if clickable
			/*102*/ uint8 bRemainOpen;
			/*103*/ uint8 bVisible; //1 is visible
			/*104*/ uint8 bHeadingChanged;
			/*105*/ uint8 padding1[3];
			/*108*/ float TopSpeed1;
			/*112*/ float TopSpeed2;
			/*116*/ uint8 bNeedsTimeStampSet;
			/*117*/ uint8 padding2[3];
			/*120*/ float unknownFloat1;
			/*124*/ float unknownFloat2;
			/*128*/ uint8 unknownByte1;
			/*129*/ uint8 padding3[3];
			/*132*/
		};

		struct ZonePoint_Entry {
			/*00*/ uint32 iterator;
			/*04*/ float y;
			/*08*/ float x;
			/*12*/ float z;
			/*16*/ float heading;
			/*20*/ uint16 zoneid;
			/*22*/ uint16 zoneinstance;
			/*24*/ uint32 unknown024;
			/*28*/ uint32 unknown028;
			/*32*/
		};

		struct ZonePoints {
			/*00*/ uint32 count;
			/*04*/ struct ZonePoint_Entry zpe[0]; // Always add one extra to the end after all zonepoints
		};

		struct EnterWorld_Struct {
			/*000*/	char	name[64];
			/*064*/	int32	unknown1;
			/*068*/	int32	unknown2; //laurion handles these differently so for now im just going to ignore them till i figure it out
		};

		struct ZoneChange_Struct {
			/*000*/	char	char_name[64];     // Character Name
			/*064*/	uint16	zoneID;
			/*066*/	uint16	instanceID;
			/*068*/	uint32  Unknown068;
			/*072*/	uint32  Unknown072;
			/*076*/	float	y;
			/*080*/	float	x;
			/*084*/	float	z;
			/*088*/	uint32	zone_reason;	//0x0A == death, I think
			/*092*/	int32	success;		// =0 client->server, =1 server->client, -X=specific error
			/*096*/ uint32	Unknown096;	// Not sure the extra 4 bytes goes here or earlier in the struct.
			/*100*/
		};

		struct RequestClientZoneChange_Struct {
			/*000*/	uint16	zone_id;
			/*002*/	uint16	instance_id;
			/*004*/	uint32	unknown004;
			/*008*/	float	y;
			/*012*/	float	x;
			/*016*/	float	z;
			/*020*/	float	heading;
			/*024*/	uint32	type;	//unknown... values
			/*032*/	uint8	unknown032[144];
			/*172*/	uint32	unknown172;
			/*176*/
		};

		struct WearChange_Struct {
			/*000*/ uint32 spawn_id;
			/*004*/ uint32 wear_slot_id;
			/*008*/ uint32 armor_id;
			/*012*/ uint32 variation;
			/*016*/ uint32 material;
			/*020*/ uint32 new_armor_id;
			/*024*/ uint32 new_armor_type;
			/*028*/ uint32 color;
			/*032*/
		};

		struct ExpUpdate_Struct
		{
			/*000*/ uint64 exp; //This is exp % / 1000 now; eg 69250 = 69.25%
			/*008*/ uint64 unknown; //unclear, I didn't see the client actually read this value but i might have missed it
		};

		struct DeleteSpawn_Struct
		{
			/*00*/ uint32 spawn_id;		// Spawn ID to delete
			/*04*/ uint8 unknown04;		// Seen 1
			/*05*/
		};

		//OP_SetServerFilter
		struct SetServerFilter_Struct {
			uint32 filters[68];
		};

		// Was new to RoF2, doesn't look changed
		// The padding is because these structs are padded to the default 4 bytes
		struct InventorySlot_Struct
		{
			/*000*/	int16 Type;
			/*002*/	int16 Padding1;
			/*004*/	int16 Slot;
			/*006*/	int16 SubIndex;
			/*008*/	int16 AugIndex;
			/*010*/	int16 Padding2;
			/*012*/
		};

		// Was new for RoF2 - Used for Merchant_Purchase_Struct, doesn't look changed
		// Can't sellfrom other than main inventory so Slot Type is not needed.
		// The padding is because these structs are padded to the default 4 bytes
		struct TypelessInventorySlot_Struct
		{
			/*000*/	int16 Slot;
			/*002*/	int16 SubIndex;
			/*004*/	int16 AugIndex;
			/*006*/	int16 Padding;
			/*008*/
		};

		// OP_MoveMultipleItems (CInvSlotMgr::MoveItem / MultipleItemMoveManager::ProcessMove).
		// Entry layout confirmed from the client: four 8-byte chunks plus a trailing 4-byte field.
		// The slot structs carry the client's own inventory numbering, not EQEmu's canonical one.
		struct MultiMoveItemSub_Struct
		{
			/*0000*/ InventorySlot_Struct	from_slot;
			/*0012*/ InventorySlot_Struct	to_slot;
			/*0024*/ uint32				number_in_stack;
			/*0028*/ uint32				flag;
			/*0032*/ uint32				unknown;
			/*0036*/
		};

		struct MultiMoveItem_Struct
		{
			/*0000*/ uint32				count;
			/*0004*/ MultiMoveItemSub_Struct moves[0];
		};

		// OP_FeatureList (0x4451) - feature/entitlement list.
		// Client dispatcher: FUN_1401d5140 case 0x4451. Reads a flag byte, an entry count, then
		// count * (feature id, value) pairs into the feature map at pinstLocalPC + 0x26b0.
		// Feature 0x1eb472 (1298034) is what lets the client use general inventory slots 11 and 12.
		// The wire layout is byte packed, so it matches EQEmu's FeatureList_Struct exactly.
		struct FeatureEntry_Struct
		{
			/*0000*/ uint32				feature_id;
			/*0004*/ uint32				value;
			/*0008*/
		};

		struct FeatureList_Struct
		{
			/*0000*/ uint8				refresh_ui;
			/*0001*/ uint32				count;
			/*0005*/ FeatureEntry_Struct	entries[0];
		};

		struct Consider_Struct {
			/*000*/ uint32	playerid;               // PlayerID
			/*004*/ uint32	targetid;               // TargetID
			/*008*/ uint32	faction;                // Faction
			/*012*/ uint32	level;					// Level
			/*016*/ uint32	report_mode;			// 0 normally, 4 will do a more detailed report that only works if you have GM flag set
			/*020*/ uint8	rare_creature;			// Will do the rare creature string
			/*021*/ uint8	loot_locked;			// Will list the target as (loot locked)
			/*022*/ uint8	unknown022;				// Padding probably
			/*023*/ uint8	unknown023;				// Padding probably
			/*024*/
		};

		struct ChangeSize_Struct
		{
			/*00*/ uint32 EntityID;
			/*04*/ float Size;
			/*08*/ uint32 Unknown08;	// Observed 0
			/*12*/ float Unknown12;		// Observed 1.0f
			/*16*/
		};

		struct SpawnHPUpdate_Struct
		{
			/*00*/ int16	spawn_id;
			/*02*/ int64	cur_hp;
			/*10*/ int64	max_hp;
			/*18*/
		};

		struct ClickDoor_Struct {
			/*00*/ uint16 player_id;
			/*02*/ uint8 padding1[2];
			/*04*/ int32 unknown1;
			/*08*/ int32 unknown2;
			/*12*/ uint8 doorid;
			/*13*/ uint8 padding2[3];
		};

		/*
		Flags for special:
		WildRampage: 0x1
		Rampage: 0x2
		NoCastOnText: 0x4
		DoubleBowShot: 0x8
		UnknownSpellFlag: 0x10
		Flurry: 0x20
		Riposte: 0x40
		Critical: 0x80
		Lucky: 0x100
		FinishingBlow: 0x200
		CripplingBlow: 0x400
		Assassinate: 0x800
		DeadlyStrike: 0x1000
		SlayUndead: 0x2000
		Headshot: 0x4000
		Strikethrough: 0x8000
		LuckyRiposte: 0x10000
		Twincast: 0x20000
		Might be more flags beyond this but I'm not sure
		*/

		struct CombatDamage_Struct
		{
			/*000*/ uint16 target;
			/*002*/ uint16 source;
			/*004*/ uint32 unknown1; //not read by the client
			/*008*/ int64 damage;
			/*016*/ uint32 special; //flags; will document above
			/*020*/ int32 spellid;
			/*024*/ uint32 spell_level; //spell caster level (unconfirmed; it is used for the spell link)
			/*028*/ float force; //I haven't actually been able to confirm these three yet
			/*032*/ float hit_heading;
			/*036*/ int32 hit_pitch;
			/*040*/ uint8 type;
			/*041*/ uint8 padding[3];
			/*044*/ uint32 unknown2; //not read by the client
			/*048*/ 
		};

		struct Animation_Struct {
			/*00*/	uint16 spawnid;
			/*02*/	uint8 action;
			/*03*/	uint8 speed;
			/*04*/
		};

		struct Death_Struct
		{
			/*000*/	uint32	spawn_id;
			/*004*/	uint32	killer_id;
			/*008*/	uint32	corpseid; //not read by client
			/*012*/	uint32	unknown1; //not read by client
			/*016*/	uint32	spell_id;
			/*020*/ uint32	attack_skill;
			/*024*/	uint64	damage;
			/*032*/	uint32	unknown2; //not read by client
			/*036*/	uint32	unknown3; //not read by client
			/*040*/
		};

		struct DeleteItem_Struct
		{
			/*0000*/ InventorySlot_Struct	from_slot;
			/*0012*/ InventorySlot_Struct	to_slot;
			/*0024*/ uint32			number_in_stack;
			/*0028*/
		};

		struct MoveItem_Struct
		{
			/*0000*/ InventorySlot_Struct	from_slot;
			/*0012*/ InventorySlot_Struct	to_slot;
			/*0024*/ uint32			number_in_stack;
			/*0028*/
		};

		struct MerchantClickRequest_Struct
		{
			/*000*/ uint32 npc_id;      // Merchant NPC's entity id
			/*004*/
		};

		// Laurion merchant window open/close response.
		// Client handler: ZonePacket__dispatchRecv case 0x840 -> FUN_1401d4fa0.
		//   +0 must resolve to a spawn (the merchant), +4 is read as a byte flag:
		//   0 = close/clear the window, non-zero = open it. Everything after +4 is
		//   handed to FUN_14045c750 (greed float, tab bitmask, ldon cat, alt currencies, flag).
		struct MerchantClickResponse_Struct
		{
			/*000*/ uint32 npc_id;      // Merchant NPC's entity id
			/*004*/ uint32 action;      // 0 = close window, non-zero = open window
			/*008*/ float rate;         // merchant greed / price multiplier
			/*012*/ uint32 tab_display; // bitmask b000 none, b001 Purchase/Sell, b010 Recover, b100 Parcels
			/*016*/ uint32 ldon_category; // ldon cat for ldon merchants
			/*020*/ uint32 alt_currency1; //These two usually match but I imagine they could be different?
			/*024*/ uint32 alt_currency2;
			/*028*/ uint32 unknown028; // Observed 256
			/*032*/
		};

		struct BeginCast_Struct
		{
			/*000*/	uint32 spell_id;
			/*004*/	uint16 caster_id;
			/*006*/	uint32 cast_time; // in miliseconds
			/*010*/	uint32 unknown0a; // I think this is caster effective level but im not sure. live always sends 0
			/*014*/	uint8 unknown0e; // 0 will short circuit the cast, seen 1 from live usually, maybe related to interrupts or particles or something
			/*015*/
		};

		//I've observed 5 s16 that are all -1.
		//Clicky items don't even trigger this as far as i can tell so not sure what this is for now.
		//One of these could have changed to a s32 but im not sure.
		struct CastSpellInventorySlot_Struct {
			/*00*/ int16 type;
			/*02*/ int16 slot;
			/*04*/ int16 sub_index;
			/*06*/ int16 aug_index;
			/*08*/ int16 unknown1;
			/*10*/
		};

		struct CastSpell_Struct
		{
			/*00*/	uint32	slot;
			/*04*/	uint32	spell_id;
			/*08*/	CastSpellInventorySlot_Struct inventory_slot; 
			/*18*/	uint32	target_id;
			/*22*/	uint32	spell_crc; 
			/*26*/  float y_pos;
			/*30*/  float x_pos;
			/*34*/  float z_pos;
			/*38*/	uint8 unknown; //not sure, might also be before y_pos; only ever seen zero for both but should be easy to figure out later
			/*39*/
		};

		struct EQAffectSlot_Struct {
			/*00*/ int32 slot;
			/*04*/ int32 padding;
			/*08*/ int64 value;
			/*16*/
		};

		struct EQAffect_Struct
		{
			/*000*/ EQAffectSlot_Struct slots[6];
			/*096*/ EqGuid caster_id;
			/*104*/ uint32 flags;
			/*108*/ uint32 spell_id;
			/*112*/ uint32 duration;
			/*116*/ uint32 initial_duration;
			/*120*/ uint32 hit_count;
			/*124*/ uint32 viral_timer;
			/*128*/ float modifier;
			/*132*/ float y;
			/*136*/ float x;
			/*140*/ float z;
			/*144*/ uint8 level;
			/*145*/ uint8 type;
			/*146*/ uint8 charges; //no idea if these are right; eqlib doesn't seem to know either
			/*147*/ uint8 activatable;
			/*148*/ uint32 unknown1; //might be some timer, not sure though
			/*152*/
		};

		struct EQAffectPacket_Struct {
			/*000*/ uint32 entity_id;
			/*004*/ int32 unknown004;
			/*008*/ EQAffect_Struct affect;
			/*160*/ uint32 slot_id;
			/*164*/ uint32 buff_fade;
			/*168*/
		};

		struct ManaChange_Struct
		{
			uint32 new_mana;
			uint32 stamina;
			uint32 spell_id;
			uint32 keepcasting;
			int32 slot;
		};

		// OP_MemorizeSpell (0x1d31), 16 byte payload. Field order matches the client handler
		// FUN_1401fbb40 in eqgame.exe: slot, spell_id, action, reduction.
		// `action` is a LaurionSpellAction value (0 scribe / 1 book repaint / 2 memorize /
		// 3 unmemorize / 4 recast timer / -1 full resync), not EQEmu's semantic ScribeSpellActions.
		struct MemorizeSpell_Struct
		{
			/*00*/ uint32 slot;      // spellbook slot or spell bar slot, depending on action
			/*04*/ uint32 spell_id;
			/*08*/ uint32 action;    // LaurionSpellAction
			/*12*/ uint32 reduction; // recast reduction in ms (action 4 only)
		};

		//This is what we call OP_Action
		//To the client though this is basically a missile hit though
		//OP_Action is basically "instant missile hit" to the client
		//@0x1401f0970 MissileHitInfo::Deserialize(CUnSerializeBuffer *buffer);
		struct MissileHitInfo
		{
			uint16 target;
			uint16 source;
			uint32 spell_id;
			//4 leaves a buff
			uint32 effect_type; 
			uint32 effective_casting_level;
			//Client does read this but only does something if it's negative
			int64 unknown1;
			//I don't see the client read this one outside basic serialization
			int64 unknown2;
			//I don't see the client read this one either but based on captures from live it seems to match spell damage
			int64 damage;
			float modifier;
			float force;
			float hit_heading;
			float hit_pitch;
			//same convention as damage
			//231 for spell, otherwise it's skill in use
			uint8 skill; 
			uint8 level; //the client doesn't actually deserialize anything past level
			//live however has a lot more info here depending on packet type
		};

		struct MobHealth_Struct
		{
			/*01*/ int16 spawn_id;
			/*00*/ uint32 hp;
		};

		struct GMTrainee_Struct
		{
			/*000*/ uint32 npcid;
			/*004*/ uint32 playerid;
			/*008*/ uint32 skills[PACKET_SKILL_ARRAY_SIZE];
			/*408*/ uint8 unknown408[36];
			/*444*/
		};

		struct GMTrainSkillConfirm_Struct {
			/*000*/	uint32	SkillID;
			/*004*/	uint32	Cost;
			/*008*/	uint8	NewSkill;	// Set to 1 for 'You have learned the basics' message.
			/*009*/	char	TrainerName[64];
			/*073*/ uint8	Unknown073[3];
			/*076*/
		};

		struct SkillUpdate_Struct {
			/*00*/	uint32 skillId;
			/*04*/	uint32 value;
			/*08*/	uint8 active;
			/*09*/	uint8 padding[3];
			/*12*/
		};

		struct AA_Array
		{
			uint32 AA;
			uint32 value;
			uint32 charges;	// expendable charges
			bool bUnknown0x0c; // added test winter 2024; removed sometime in summer 2024
		};

		struct AATable_Struct {
			/*00*/ uint32 aa_spent;				// Total AAs Spent
			/*04*/ uint32 aapoints_assigned[6];	// none, general, arch, class, special, focus, merc
			/*24*/ AA_Array aa_list[MAX_PP_AA_ARRAY];
		};

		struct AltAdvStats_Struct {
			/*000*/	uint32 experience;
			/*004*/	uint32 unspent;
			/*008*/	uint8 percentage;
			/*009*/	uint8 unknown009[3];
		};

		struct BlockedBuffs_Struct
		{
			/*000*/ int32 SpellID[BLOCKED_BUFF_COUNT];
			/*120*/ uint32 Count;
			/*124*/ uint8 Pet;
			/*125*/ uint8 Initialise;
			/*126*/ uint16 Flags;
		};

		struct ZonePlayerToBind_Struct {
			//Same structure as the binds in PlayerProfile_Struct
			//Assembly calls the same function
			/*000*/	uint16	bind_zone_id;
			/*002*/	uint16	bind_instance_id;
			/*004*/	float	x;
			/*008*/	float	y;
			/*012*/	float	z;
			/*016*/	float	heading;
			/*020*/	char	zone_name[1];  // Or "Bind Location"
			/*021*/	uint32	unknown1;
			/*025*/	uint32	unknown2;
			/*029*/	uint32	unknown3;
		};

		struct ArmorPropertyStruct
		{
			/*000*/ uint32 type;
			/*004*/ uint32 variation;
			/*008*/ uint32 material;
			/*012*/ uint32 newArmorID;
			/*016*/ uint32 newArmorType;
			/*020*/
		};

		struct Illusion_Struct {
			/*000*/ uint32 spawnid;
			/*004*/ char charname[64];
			/*068*/ uint16 race; //according to eqlib this is s32
			/*070*/ char unknown006[2];
			/*072*/ uint8 gender;
			/*073*/ uint8 texture;
			/*074*/ uint8 armorVariation;
			/*075*/ uint8 armorMaterial;
			/*076*/ uint8 helmtexture;
			/*077*/ uint8 unknown077; //padding from this being a pack(4) struct actually
			/*078*/ uint8 unknown078;
			/*079*/ uint8 unknown079;
			/*080*/ uint32 face;
			/*084*/ uint8 hairstyle;
			/*085*/ uint8 haircolor;
			/*086*/ uint8 beard;
			/*087*/ uint8 beardcolor;
			/*088*/ float size;
			/*092*/ uint32_t npc_tint;
			/*096*/ bool keep_armor_properties;
			/*097*/ uint8 unknown097[3]; //padding from this being a pack(4) struct actually
			/*100*/ ArmorPropertyStruct armorProperties[9];
			/*280*/ uint32_t armorTints[9];
			/*316*/ int32 class_;
			/*320*/ uint32 drakkin_heritage;
			/*324*/ uint32 drakkin_tattoo;
			/*328*/ uint32 drakkin_details;
			/*332*/
		};

		struct moneyOnCorpseStruct {
			/*000*/ uint8 type;
			/*001*/ uint8 padding1[3];
			/*004*/ uint32 flags;
			/*008*/ uint32 platinum;
			/*012*/ uint32 gold;
			/*016*/ uint32 silver;
			/*020*/ uint32 copper;
			/*024*/
		};

		// Live capture (x64dbg, 2026-10-10): the Laurion client sends a 152 byte payload for
		// OP_GroupInvite / OP_GroupInvite2 - the two names plus a 24 byte tail. Every byte of the tail
		// that the client writes is a constant, and 5 of the 24 bytes are never written at all.
		// See docs/op_groupinvite-investigation.md.
		struct GroupInvite_Struct {
			/*0000*/ char	invitee_name[64];
			/*0064*/ char	inviter_name[64];
			/*0128*/ uint32	unknown0128;
			/*0132*/ uint32	unknown0132;
			/*0136*/ uint32	unknown0136;
			/*0140*/ uint32	unknown0140;
			/*0144*/ uint32	unknown0144;
			/*0148*/ uint32	unknown0148;	// observed 0x100 on the /invite path
			/*0152*/
		};

		// Opcode 0x78ef (OP_GroupDisband) - the client's disband/leave request, sent by FUN_140257290 from the
		// group window, the hot button and /ungroup. Same 64 + 64 + 24 shape as GroupInvite_Struct: +0 is the
		// local player name, +64 is the other member name. EQEmu's Handle_OP_GroupDisband requires exactly
		// sizeof(GroupGeneric_Struct) = 128, so the 152 byte Laurion packet was rejected and the disband
		// never ran at all.
		struct GroupDisband_Struct {
			/*0000*/ char	 name1[64];
			/*0064*/ char	 name2[64];
			/*0128*/ uint32	 unknown0128;
			/*0132*/ uint32	 unknown0132;
			/*0136*/ uint32	 unknown0136;
			/*0140*/ uint32	 unknown0140;
			/*0144*/ uint32	 unknown0144;
			/*0148*/ uint32	 unknown0148;
			/*0152*/
		};

		// Opcodes 0x8de (OP_GroupFollow) and 0x70b7 (OP_GroupFollow2) - the group invite accept, sent by
		// FUN_140260420 when the invitee clicks FOLLOW in the group window (also reachable from /follow and
		// the hot button). Payload 156 bytes. The u32 at +152 is the value the client stored from the invite
		// packet's +152 (DAT_140eb0ba0) and echoes it back unchanged.
		// FUN_140260420 emits 0x8de when DAT_140eb0b9c is set and 0x70b7 otherwise; DAT_140eb0b9c is set by
		// the invite handler FUN_1402827f0 only for an invite arriving on 0x1d90, so the two values are the
		// same packet selected by which invite opcode was used, not a first/second pass.
		struct GroupFollowAccept_Struct {
			/*0000*/ char	inviter_name[64];
			/*0064*/ char	invitee_name[64];
			/*0128*/ uint16	unknown0128;
			/*0130*/ uint32	unknown0130;
			/*0134*/ uint32	unknown0134;
			/*0138*/ uint32	unknown0138;
			/*0142*/ uint32	unknown0142;
			/*0146*/ uint16	unknown0146;	// observed 0x100 on the client send path
			/*0148*/ uint32	unknown0148;
			/*0152*/ uint32	echoed_from_invite;
			/*0156*/
		};

		// Opcode 0x1f25 (OP_GroupCancelInvite) - the group invite response, BOTH direction.
		// Send sites: FUN_140257290 (the invitee declines: prints eqstr 12289 "You cancel the invitation
		// to join %1's group.", subcommand 3, then clears InvitedToGroup) and FUN_1402827f0 (the invite
		// handler auto-declines with 1 / 4 / 5 when it will not accept the invite).
		// Receive handler FUN_140282be0 reads the subcommand byte at +152 and the name at +64 and prints
		// eqstr 12264 / 12265 / 12266 / 5086 / 5088 about that name - i.e. the inviter sees the decline.
		// The client writes the subcommand only at +152 (as a u32 with the value in the low byte); the
		// dword at +128 has its low byte explicitly zeroed by both senders, and +144 holds a constant 0x100.
		// EQEmu's GroupCancel_Struct is 129 bytes with the toggle at +128, so the subcommand has to move
		// between +152 and +128 in both directions. Reverse notes: docs/group-opcodes-laurion.md
		struct GroupCancelInvite_Struct {
			/*0000*/ char   name1[64];		// the inviter
			/*0064*/ char   name2[64];		// the invitee - the %1 the receiver prints
			/*0128*/ uint32 unknown0128;	// low byte explicitly zeroed by the client senders
			/*0132*/ uint32 unknown0132;
			/*0136*/ uint32 unknown0136;
			/*0140*/ uint32 unknown0140;
			/*0144*/ uint32 unknown0144;	// observed 0x100 in the low u16
			/*0148*/ uint32 unknown0148;
			/*0152*/ uint32 subcommand;	// 1 already invited, 2 already grouped, 3 declined,
									// 4 raid, 5 raid group leader - read as a byte by FUN_140282be0
			/*0156*/
		};

		// Opcode 0x7e71 (OP_GroupInvitePrompt), server -> client. Handler FUN_1403aa570 reads only the
		// u64 at payload+0x18 and resolves the inviter through PlayerManagerClient::GetPlayerFromHashKey
		// (vtable slot 3), then shows the fellowship invite popup. There is no size gate, so 32 bytes is
		// enough. The field names before +0x18 mirror the FellowshipUpdate struct prefix (LS/opcodes/
		// batch_06.md) and are not read by this handler - they are placeholders, not confirmed layout.
		struct GroupInvitePrompt_Struct {
			/*000*/ uint32	subcommand;	// 5 = invite on the client send path
			/*004*/ uint32	member_index;
			/*008*/ EqGuid	fellowship_guid;
			/*016*/ EqGuid	invitee_guid;
			/*024*/ EqGuid	inviter_guid;	// read at payload+0x18 by FUN_1403aa570
			/*032*/
		};

		// Opcode 0x7d5b (OP_KeyRingSlots), server -> client, 10 byte body. Dispatcher branch 0x1401e303b
		// (CMP 0x7d5b at 0x1401d7018) calls FUN_1401fb9d0(payload, payload_size), which writes five u16
		// straight into pinstLocalPC->BaseKeyRingSlots[0..4] (PCClient + 0x2EE8). There is no count field
		// and no size gate: a body shorter than 10 bytes repeats its last value into the remaining slots,
		// so always send all five. The client reads them back as signed short, so keep every value at or
		// below 32767. Reverse notes: docs/op_7d5b_keyring_slots.md
		struct KeyRingSlots_Struct {
			/*000*/ uint16	mount_keyring_slots;
			/*002*/ uint16	illusion_keyring_slots;
			/*004*/ uint16	familiar_keyring_slots;
			/*006*/ uint16	heroforge_keyring_slots;
			/*008*/ uint16	teleportation_keyring_slots;
			/*010*/
		};

		struct GroupGeneric_Struct {
			/*0000*/ char name1[64];
			/*0064*/ char name2[64];
			/*0128*/ uint32	unknown0128;
			/*0132*/ uint32	unknown0132;
			/*0136*/ uint32	unknown0136;
			/*0140*/ uint32	unknown0140;
			/*0144*/ uint32	unknown0144;
			/*0148*/ uint32	unknown0148;
			/*0152*/ uint16	unknown0152;
			/*0154*/
		};

		struct AugmentInfo_Struct
		{
			/*000*/ uint32	itemid; // id of the solvent needed
			/*004*/ uint32	window;	// window to display the information in
			/*008*/ char	augment_info[64]; // total packet length 80, all the rest were always 00
			/*072*/ uint32	unknown072; // seen 0, 56
			/*076*/ uint32	unknown076; // seen 8, 3, 11, always matches what client sends
			/*080*/
		};

		//seems to be unchanged from rof2?
		//it's the same size at least
		struct AugmentItem_Struct {
			/*00*/	uint32	dest_inst_id;			// The unique serial number for the item instance that is being augmented
			/*04*/	uint32	container_index;				// Seen 0
			/*08*/	InventorySlot_Struct container_slot;	// Slot of the item being augmented
			/*20*/	uint32	augment_index;				// Seen 0
			/*24*/	InventorySlot_Struct augment_slot;	// Slot of the distiller to use (if one applies)
			/*36*/	int32	augment_action;			// Guessed - 0 = augment, 1 = remove with distiller, 3 = delete aug
			/*36*/	//int32	augment_slot;
			/*40*/
		};

		struct ApplyPoison_Struct
		{
			TypelessInventorySlot_Struct inventorySlot;
			uint32 success;
		};

		/*
		** Click Object Struct (client -> server, echoed back server -> client)
		** CEverQuest__LMouseUp (FUN_140264b70), send site 0x140266293: clicking a ground item.
		** Wire 14 bytes = 2 byte opcode + 12 byte payload, sent through CConnection__sendPacket
		** (FUN_140523530, channel 4) from the shared buffer at DAT_140e35fa0.
		**   +0  u32  drop_id   - ground item record + 0x20. FUN_14020b530 (the OP_GroundSpawn reader)
		**                        stores the first u32 of the spawn packet there, and FUN_14020b4a0
		**                        looks a record up by that same field.
		**   +4  u32  player_id  - pinstLocalPlayer + 0x168 (the local player's spawn id)
		**   +8  u8   flag       - written as 0 by the click path; the client only zeroes the low byte
		**                        (MOV byte [RBP+0x70], 0 then reads back a dword), so +9..+11 are
		**                        stale bytes from the shared send buffer. Do not trust them.
		** EQEmu's ClickObject_Struct is 8 bytes (drop_id + player_id) - Handle_OP_ClickObject rejects
		** the 12 byte packet with "Invalid size on ClickObject_Struct" and the pickup never happens.
		*/
		struct ClickObject_Struct {
			/*00*/	uint32	drop_id;
			/*04*/	uint32	player_id;
			/*08*/	uint8	unknown08;	// flag, seen 0 from the client
			/*09*/	uint8	unknown09[3];	// uninitialised in the client
			/*12*/
		};

		/*
		** Click Object Acknowledgement Struct
		** Response to client clicking on a World Container (ie, forge)
		** Seems to have not changed from RoF2
		*/
		struct ClickObjectAction_Struct {
		/*00*/  //uint32 player_id;	// Appears to have been removed
		/*00*/	uint32	drop_id;	// Appears to use the object_count field now
		/*04*/	int32	unknown04;	// Seen -1
		/*08*/	int32	unknown08;	// Seen -1
		/*08*/	//uint32 open;		// 1=opening, 0=closing - Removed?
		/*12*/	uint32	type;		// See object.h, "Object Types"
		/*16*/	uint32	unknown16;	//
		/*20*/	uint32	icon;		// Icon to display for tradeskill containers
		/*24*/	uint32	unknown24;	//
		/*28*/	char	object_name[64]; // Object name to display
		/*92*/
		};

		//received and sent back as an ACK with different reply_code
		struct RecipeAutoCombine_Struct {
			/*00*/	uint32 object_type;
			/*04*/	uint32 some_id;
			/*08*/	InventorySlot_Struct container_slot;		//echoed in reply - Was uint32 unknown1
			/*20*/	InventorySlot_Struct unknown_slot;		//echoed in reply
			/*32*/	uint32 recipe_id;
			/*36*/	uint32 reply_code;
			/*40*/
		};

		/*
		** New Combine Struct
		** Client requesting to perform a tradeskill combine
		** Size: 24 bytes
		** Used In: OP_TradeSkillCombine
		** Last Updated: 01-05-2013
		*/
		struct NewCombine_Struct
		{
			/*00*/	InventorySlot_Struct container_slot;
			/*12*/	InventorySlot_Struct guildtribute_slot;	// Slot type is 8? (MapGuildTribute = 8)
			/*24*/
		};

		struct Disciplines_Struct {
			uint32 values[MAX_PP_DISCIPLINES];
		};

		// Buy request, opcode 0x625e (client -> server).
		// Send site: CMerchantWnd__PurchasePageHandler__RequestGetItem @ 0x14045cec0, wire length 0x1a (24).
		// +8 is a 64 bit value (the client's ItemBase::MerchantSlot at +0xF0), which is the merchant list
		// slot for merchant stock.
		// Note: the normal sell does NOT use this shape. It goes through FUN_1404561f0 event 0, which sends
		// opcode 0x6489 as a 16 byte payload (npcid, slot struct, quantity) - see
		// structs::Merchant_Purchase_Request_Struct.
		struct Merchant_Sell_Request_Struct {
			/*000*/ uint32 npcid;		// Merchant NPC's entity id
			/*004*/ uint32 playerid;	// Player's entity id
			/*008*/ uint64 itemslot;	// Merchant Slot / Item Instance ID
			/*016*/ uint32 quantity;	// Already sold
			/*020*/ uint32 unknown020;
			/*024*/
		};

		struct Merchant_Sell_Response_Struct {
			/*000*/ uint32 npcid;		// Merchant NPC's entity id
			/*004*/ uint32 playerid;	// Player's entity id
			/*008*/ uint32 itemslot;	// Merchant Slot / Item Instance ID
			/*012*/ uint32 unknown12;
			/*016*/ uint32 quantity;	// Already sold
			/*020*/ uint32 unknown20;
			/*024*/ uint32 price;
			/*028*/ uint32 unknown28;	// Normally 0, but seen 84 c5 63 00 as well
			/*032*/
		};

		struct Merchant_Purchase_Request_Struct {
			/*000*/	uint32	npcid;			// Merchant NPC's entity id
			/*004*/	TypelessInventorySlot_Struct	inventory_slot;
			/*012*/	uint32	quantity;
			/*016*/	
		};

		// Sell ack, opcode 0x6489 (server -> client).
		// Handler: ZonePacket__dispatchRecv case 0x6489 -> FUN_1401d5100 -> FUN_140458b70.
		// There is no npcid in this packet - the slot struct starts at +0, quantity at +8 and the
		// 64 bit price at +16. slot == -1 makes the client print string 12062 (merchant refusal).
		struct Merchant_Purchase_Response_Struct {
			/*000*/	TypelessInventorySlot_Struct	inventory_slot;
			/*008*/	uint32	quantity;
			/*012*/	uint32	unknown012;
			/*016*/	uint64	price;
			/*024*/
		};

		// Parcel send, opcode 0x0f16 (client -> server).
		// Send site: FUN_14045e7e0, wire length 0xe2 (226) = 2 byte opcode + 224 byte payload.
		// The client's slot struct is split by the quantity: slot +4, sub +6, quantity +8, aug +12,
		// then a u16 that is always 0xffff at +14, the money/item flag at +16, send_to at +20 and
		// note at +84. EQEmu's canonical Parcel_Struct is 220 bytes with send_to at +16 and note[128]
		// at +80, so Handle_OP_ShopSendParcel rejected every Laurion parcel on the size check, and its
		// uint32 item_slot is really slot+sub packed together.
		struct Parcel_Request_Struct {
			/*000*/	uint32	npc_id;
			/*004*/	int16		slot;
			/*006*/	int16		sub;
			/*008*/	uint32	quantity;
			/*012*/	int16		aug;
			/*014*/	int16		unknown014;	// 0xffff in the client
			/*016*/	uint32	money_flag;
			/*020*/	char		send_to[64];
			/*084*/	char		note[140];
			/*224*/
		};

		// 0x1634 MerchantSellItem (12 bytes) - shape matches the canonical struct, no padding.
		struct MerchantSellItem_Request_Struct {
			/*000*/	uint32	npcid;
			/*004*/	int16		slot;
			/*006*/	int16		subindex;
			/*008*/	uint32	item_id;
			/*012*/
		};

		// 0x3c87 MerchantSellItemBulk (24 bytes).
		struct MerchantSellItemBulk_Request_Struct {
			/*000*/	uint32	slot_index;
			/*004*/	uint32	npcid;
			/*008*/	uint32	container0d4;
			/*012*/	int16		container0d8;
			/*014*/	int16		padding;
			/*016*/	uint32	item_id;
			/*020*/	uint32	price;
			/*024*/
		};

		// 0x7bcd merchant purchase request (16 bytes).
		struct MerchantRequestItem_Request_Struct {
			/*000*/	uint32	ldtype;
			/*004*/	uint32	spawn_id;
			/*008*/	uint32	item_id;
			/*012*/	uint32	quantity;
			/*016*/
		};

		/*
		** Cancel Trade struct
		** Sent when a player cancels a trade
		** Size: 8 bytes
		** Used In: OP_CancelTrade
		**
		*/
		struct CancelTrade_Struct {
			/*00*/	uint32 fromid;
			/*04*/	uint32 action;
			/*08*/
		};

		struct Stun_Struct { // 8 bytes total
			/*000*/	uint32	duration; // Duration of stun
			/*004*/	uint8	flag;         // SpeedHeading flag: non-zero makes the client set PlayerBase + 0x9c to 32.0f (seen 0 on live captures)
			/*005*/	uint8	unknown005; // seen 163
			/*006*/	uint8	unknown006; // seen 67
			/*007*/	uint8	unknown007; // seen 0
			/*008*/
		};
		// --- SoF x64 zone opcodes (client -> server) wire layouts ---
		// These match the emu structs in eq_packet_structs.h (no field re-ordering was
		// observed on the wire for these packets).
		struct ClientScreenSize_Struct { // OP_0x51a5, 12 byte payload
			/*00*/	uint32	width;
			/*04*/	uint32	height;
			/*08*/	uint8	flag;
			/*09*/	uint8	pad[3];
			/*12*/
		};

		struct ClientStats_Struct { // OP_0x706a, 80 byte payload, sent every 30s
			/*000*/	uint8	counters[72];
			/*072*/	uint32	value;
			/*076*/	uint32	unknown;
			/*080*/
		};

		// OP_AdvLoot (0x3175) - variable length. The payload begins with a uint16 sub-command
		// and the rest depends on that sub-command (see advloot.md section 3.3). The old
		// "member_index / state" reading was just the sub-command bytes.
		struct AdvLoot_Struct {
			/*00*/	uint16	subcmd;
		};

		// OP_AdvLootManage (0x360e) - variable length, client->server only. Same sub-command numbering as
		// OP_AdvLoot, only 0x0D / 0x0F / 0x14 are ever sent on it (see advloot.md section 10.7). Left raw:
		// no decoder is registered, so PassDecoder hands the payload to Client::Handle_OP_AdvLootManage.
		struct AdvLootManage_Struct {
			/*00*/	uint16	subcmd;
		};

		struct HoardItem_Struct { // OP_0x229c, 2 byte payload
			/*00*/	uint8	action;
			/*01*/	uint8	slot;
			/*02*/
		};

		struct HoardItemRemove_Struct { // OP_0x6d0f, 4 byte payload
			/*00*/	uint8	action;
			/*01*/	uint8	slot;
			/*02*/	uint16	unknown;
			/*04*/
		};

		struct ZoneConnectRequest_Struct { // OP_0x6d2d, 8 byte payload
			/*00*/	uint64	flag;
			/*08*/
		};

		struct LootingItem_Struct { // OP_0x0856, 20 byte payload (CLootWnd__RequestLootSlot)
			/*00*/	uint32	lootee;         // corpse entity id
			/*04*/	uint32	looter;         // player entity id
			/*08*/	uint32	slot_id;        // loot window slot index (widened from uint16)
			/*12*/	uint32	auto_loot;      // EQEmu's auto_loot flag sits here (CLootWnd__RequestLootSlot param_3)
			/*16*/	uint32	quantity;       // Laurion-only: requested stack quantity (shift/ctrl held)
			/*20*/
		};

		// OP_0x609c request. CItemDisplayWnd::SendItemLink (FUN_14040fbf0) writes the item number and
		// maker id, then appends a length prefixed name through FUN_1405645d0 (u32 length + bytes, no
		// terminator). The same opcode carries the reply back, so this is the fixed part only.
		struct ItemDisplayRequest_Struct { // OP_0x609c, variable payload (FUN_14040fbf0)
			/*00*/	uint32	item_number;    // ItemDefinition::ItemNumber (FUN_140612830)
			/*04*/	uint32	maker_id;       // ItemDefinition::MakerID (FUN_140610b80)
			/*08*/	uint32	name_length;    // length prefix, not null terminated
			/*12*/	char	name[1];
			/*xx*/
		};

		// OP_0x6159 request. CAdvancedLootWnd alt+right-click inspect path: FUN_1400A6370 ->
		// FUN_1400A6250 -> FUN_14046C5C0(DAT_140d83170 /*ItemDisplayWnd singleton*/, record[0] /*item id*/).
		// Fixed 24 byte body. CPacketScrambler__hton is the identity here (it only bumps DAT_140f0367c),
		// so 0x6159 is the wire opcode itself, same as 0x609c for FUN_14040fbf0.
		struct ItemDisplayWndRequest_Struct { // OP_0x6159, 28 byte payload (FUN_14046c5c0)
			/*00*/	uint32	item_id;        // AdvLoot row record +0x00 (param_2)
			/*04*/	uint32	unknown004;     // written 0
			/*08*/	uint32	unknown008;     // written 0
			/*12*/	uint32	unknown00C;     // written 0xFFFFFFFF
			/*16*/	uint32	unknown010;     // written 0
			/*20*/	uint32	unknown014;     // never written by the client
			/*24*/	uint32	unknown018;     // stack local masked with 0xffff0000
			/*28*/
		};

		// Laurion alt+left-click inspect request, wire opcode 0x0497 (client FUN_14040FA60, 80 bytes).
		// Layout from FUN_14040FA60: +0 item id, +4 flags (|= 2 when shift held), +8..0x47 a 64 byte
		// null terminated item link string, +0x48 and +0x4C two extra dwords.
		struct ItemLinkInspect_Struct { // OP_0x0497, 80 byte payload (FUN_14040fa60)
			/*00*/	uint32		item_id;
			/*04*/	uint32		flags;
			/*08*/	char			item_name[64];
			/*48*/	uint32		unknown048;
			/*4C*/	uint32		unknown04C;
			/*50*/
		};
		static_assert(sizeof(ItemLinkInspect_Struct) == 80, "bad size");

		// Saylink click, wire opcode 0x1e67. CChatWindow__WndNotification (FUN_1403948d0) handles WndNotify
		// 0x1b - the link-click event, the same id the old "<a WndNotify=\"27,...\">" task links used. It parses
		// the 90 character link data with FUN_1402357e0 into this record, then ORs the keyboard modifier flags
		// from CXWndManager__GetKeyboardFlags into the +0x20 dword (4 always, 8 shift+ctrl, 0x10 alt, 2 plain)
		// and sends 84 bytes. Field offsets are the write sites in FUN_1402357e0: item id at +0, the six
		// augments at +4, hash at +0x1C, is_evolving at +0x24, lore group +0x28, evolve level +0x2C, ornament
		// +0x30, luck +0x34 and the six socket luck values at +0x38.
		struct SayLinkClick_Struct { // OP_0x1e67, 84 byte payload (FUN_1403948d0 notify 0x1b)
			/*00*/	uint32		item_id;
			/*04*/	uint32		augments[6];
			/*1C*/	uint32		hash;
			/*20*/	uint32		click_flags;
			/*24*/	uint8		is_evolving;
			/*25*/	uint8		unknown025[3];
			/*28*/	uint32		evolve_group;
			/*2C*/	uint32		evolve_level;
			/*30*/	uint32		ornament_icon;
			/*34*/	uint32		luck;
			/*38*/	uint32		socket_luck[6];
			/*50*/	uint32		unknown050;   // the dword at [RBP-0x10]: zeroed by the caller, written by no
												// parser, sent anyway. Reserved field, always 0 on this path.
			/*54*/
		};
		static_assert(sizeof(SayLinkClick_Struct) == 84, "bad size");

		// CItemDisplayWnd::SetItem flags, MacroQuestLS src/eqlib/UI.h L4484 ItemDisplayFlags.
		enum ItemDisplayFlags : uint32_t {
			ItemDisplayPreventLink      = 0x00000001,
			ItemDisplayRecycleWindow    = 0x00000002,
			ItemDisplayFromLink         = 0x00000004,
			ItemDisplayFromBazaarSearch = 0x00000008,   // ShowItem routes these two to another manager
			ItemDisplayFromBarterSearch = 0x00000010,   // (FUN_140410DE0 flags & 0x08 / & 0x10)
		};

		// OP_0x0497 reply, server -> client on the same opcode the request used. Read by
		// ZonePacket__dispatchRecv case 0x1401d90fb -> FUN_140200390, which reads the three dwords and
		// then hands the rest of the buffer to ItemClient__CreateItemClient (0x14029D8F0). The item it
		// gets back goes straight to CItemDisplayManager::ShowItem (FUN_140410DE0) -> CItemDisplayWnd::
		// SetItem, which is the only writer of CItemDisplayWnd::pItem (window +0x3F8).
		struct ItemLinkInspectReply_Struct { // OP_0x0497 reply header, then the serialized item
			/*00*/	int32		luck;         // -> ItemBase::Luck (+0x100); applied only when the definition
												// has a luck range (FUN_14060E440: MaxLuck >= MinLuck, not both 0)
			/*04*/	int32		charges;      // -> FUN_140619140 -> ItemBase::Charges (+0xB8); applied only when
												// > 0, clamped to 100 and to ItemSpellData::Spells[0].MaxCharges (+0x22C),
												// and forced to 0 when Spells[0].SpellID (+0x220) is -1
			/*08*/	uint32	display_flags;  // ItemDisplayFlags, passed unchanged to ShowItem
			/*0C*/	// serialized item follows, byte for byte what SerializeItem writes for OP_ItemPacket
		};

		// OP_0x6159 reply, server -> client on the same opcode the request used. Read by
		// ZonePacket__dispatchRecv case 0x1401e2c28: FUN_1401D1DA0 wraps the payload, then
		// ItemClient__CreateItemClient deserializes it, then FUN_1401D4A90 reads one trailing byte.
		// The holder is handed to FUN_14046C730(DAT_140d83170 /*ObjectPreviewWnd*/, &holder,
		// auto || trailing, trailing), which sets the item on the preview's inner display window
		// (ObjectPreviewWnd +0x2E8) and shows/hides its eleven child widgets (+0x348 .. +0x3A0).
		struct ItemDisplayWndReply_Struct { // OP_0x6159 reply: serialized item, then this byte
			/*00*/	uint8	show_3d_preview; // 0 -> FUN_1402BAA90 builds the actor preview; non zero ->
											   // FUN_1402BAE30 compares against the local player instead
		};

		// OP_0x1d00 wire form is variable length (null terminated GUID, then length prefixed strings),
		// so this is only the internal carrier EQEmu uses to reach the encoder. The instance pointer is
		// carried by the standard EQ::InternalSerializedItem_Struct that follows it.
		struct ItemLuck_Struct { // OP_0x1d00 internal carrier
			/*00*/	uint32	item_number;    // ItemDefinition::ItemNumber, informational
			/*04*/	uint32	luck;           // must fall inside ItemDefinition::MinLuck .. MaxLuck
			/*08*/
		};

		struct Consume_Struct { // OP_0x5ef7, 20 byte payload (FUN_1401d4020)
			/*00*/	uint32	slot;           // inventory slot of the item being consumed
			/*04*/	uint32	unknown4;       // item global index
			/*08*/	uint16	unknown8;
			/*0A*/	uint32	auto_consumed;  // 0xFFFFFFFF when auto consumed
			/*0E*/	uint32	type;           // 0x100 = food, 0x101 = water
			/*12*/
		};

		struct SkillRankReport_Struct { // OP_0x3fe5, 16 byte payload
			/*00*/	uint16	announce_flags;
			/*02*/	uint16	unknown02;      // natural alignment padding - client sends 16 bytes
			/*04*/	uint32	skill_a;
			/*08*/	uint32	skill_b;
			/*12*/	uint32	skill_c;
			/*16*/
		};

		struct TributeRequest_Struct { // OP_0x3c49 / OP_0x1e20, 4 byte payload
			/*00*/	uint32	request_type;
			/*04*/
		};
		/*
		 * Client -> server wire layouts confirmed from the LS client (opcodes/batch_*.md).
		 * Offsets are payload relative - the 2 byte opcode is stripped before decode.
		 */

		/* 0x0833 OP_GroupRoles - payload 152. EQEmu's GroupRole_Struct is 148: the client
		 * carries a dword at +144 and the toggle byte at +148 instead of +144. */
		struct GroupRole_Struct {
			/*000*/	char	Name1[64];        /* target member name (32 written + 32 zero pad) */
			/*064*/	char	Name2[64];        /* local player name */
			/*128*/	uint32	Unknown128;
			/*132*/	uint32	Unknown132;
			/*136*/	uint32	Unknown136;
			/*140*/	uint32	RoleNumber;
			/*144*/	uint32	Unknown144;
			/*148*/	uint8	Toggle;
			/*149*/	uint8	SecondBool;
			/*150*/	uint8	Unknown150[2];    /* never written by the send function */
			/*152*/
		};

		/* 0x0a83 OP_CrashDump - payload 436, every byte written by the sender */
		struct CrashDump_Struct {
			/*000*/	uint8	Unknown000[32];
			/*032*/	char	Name[64];
			/*096*/	char	ZoneName[64];
			/*160*/	uint32	Gamestate;
			/*164*/	char	GraphicsMode[16];
			/*180*/	char	StringA[128];
			/*308*/	char	StringB[128];
			/*436*/
		};

		/* 0x0afa OP_GuildMemberRankAltBanker - payload 144 */
		struct GuildMemberRankAltBanker_Struct {
			/*000*/	uint32	Unknown000;
			/*004*/	char	MyName[64];       /* zeroed by the sender */
			/*068*/	uint32	Unknown068;
			/*072*/	char	Member[56];       /* member name */
			/*128*/	uint64	Unknown128;
			/*136*/	uint32	Flags;            /* member flags: bit 0 banker, bit 1 alt */
			/*140*/	uint32	Unknown140;
			/*144*/
		};

		/* 0x0b22 OP_MercenaryDataUpdateRequest - payload 8 */
		struct MercenaryDataUpdateRequest_Struct {
			/*000*/	uint32	ManagerFieldA;
			/*004*/	uint32	ManagerFieldB;
			/*008*/
		};

		/* 0x0c05 OP_SummonCorpse - payload 64, single null terminated name */
		struct SummonCorpse_Struct {
			/*000*/	char	Name[64];
			/*064*/
		};

		/* 0x0c84 OP_GuildMemberPublicNote - payload 392 */
		struct GuildMemberPublicNote_Struct {
			/*000*/	uint64	GuildID;
			/*008*/	char	LocalName[64];
			/*072*/	char	TargetName[64];
			/*136*/	char	Note[256];
			/*392*/
		};

		/* 0x1bae OP_CrystalCreate - payload 72 */
		struct CrystalCreate_Struct {
			/*000*/	char	Name[64];
			/*064*/	uint32	Type;             /* 0 = radiant, non zero = ebon */
			/*068*/	uint32	Quantity;
			/*072*/
		};

		/* 0x2455 OP_GMLastName - payload 195 */
		struct GMLastName_Struct {
			/*000*/	char	Name[64];
			/*064*/	char	GMName[64];
			/*128*/	char	LastName[64];
			/*192*/	uint16	Unknown192;
			/*194*/	uint8	Unknown194;
			/*195*/
		};

		/* 0x2d05 OP_GuildInvite and 0x6313 OP_GuildRemove - payload 144.
		Client send site FUN_1400c3020 (0x1400c3367: MOV R9D, 0x92) and receive handler
		FUN_1401fa510 (dispatched for 0x2d05 from ZonePacket__dispatchRecv at 0x1401d99f4) both use
		the full 144 byte record. FUN_1401fa510 compares +0 against the local player name, reads the
		inviter from +64, the guild as a full 64 bit EqGuid at +128 and the rank at +136, and only
		shows the invite popup when that rank is 1..8. EQEmu's GuildCommand_Struct is 136 bytes with
		a u16 guildeqid at +128 and officer at +132, so the client sees
		guild_guid = officer << 32 | guild_id (the zone log's guid [34359738369] = 0x800000001) and
		reads the rank from outside the struct, so the popup never appears and the client falls
		through to response 9 (decline). */
		struct GuildCommand_Struct {
			/*000*/	char	OtherName[64];
			/*064*/	char	MyName[64];
			/*128*/	uint64	GuildEqID;
			/*136*/	uint32	Officer;          /* rank, 1..8 */
			/*140*/	uint32	Unknown140;
			/*144*/
		};

		/* 0x2e8e OP_BazaarSearch - the only shape stable across all send sites */
		struct BazaarSearch_Struct {
			/*000*/	uint32	Subcommand;
			/*004*/	char	ItemName[12];
			/*016*/	uint64	Unknown016;
			/*024*/	uint32	Unknown024;
			/*028*/	uint32	Unknown028;
			/*032*/
		};

		/* 0x359e OP_GuildStatus - payload 144 (EQEmu's struct has 72 here, not 80) */
		struct GuildStatus_Struct {
			/*000*/	char	Name[64];
			/*064*/	uint8	Unknown064[80];
			/*144*/
		};

		/* 0x3de1 OP_GuildBank - payload 24, C->S record as written by the client */
		/* Verified across every send site (CGuild__sendNameEntryUpdate 0x1403c37e0,
		/* FUN_1403c5910, FUN_1403c5a30, GuildWindow__WndNotification 0x1403c5b90):
		/* the slot key is always at +12 and +8 is never written. */
		struct GuildBankRecord_Struct {
			/*000*/	uint32	Action;          /* 2, 3, 4, 5, 6, 7, 8, 10 */
			/*004*/	uint32	Serial;          /* client-side CGuild + 0xd228 */
			/*008*/	uint32	Stale008;        /* never written by the sender */
			/*012*/	uint32	Key;             /* area << 16 | slot_id */
			/*016*/	uint32	ItemID;
			/*020*/	uint32	Value;           /* quantity / permission bit / max stack size */
			/*024*/
		};

		/* 0x79d OP_GuildUpdate - S->C record, handled by FUN_1400c1250 case 0x79d.
		/* EQEmu's legacy structs (GuildUpdateUCPStruct / GuildPermission_Struct /
		/* GuildUpdateURLAndChannel_Struct) put the payload at +80: action + 76 byte gap.
		/* This client reads the payload at +88, so the gap is 84 bytes. Sending the
		/* legacy layout makes the client read the payload 8 bytes early.
		/* Client action values (switch at FUN_1400c1250+0x25f, live 0x1400c14ff):
		/*   0 = URL        -> strncpy(CGuild + 0x968, record + 88, 512)
		/*   1 = Channel    -> strncpy(CGuild + 0x968, record + 88, 512)
		/*   2 = permission bitmask for a rank (value at +88)
		/*   3 = rank name  -> rank at +88, name at +92
		/*   4 = permission -> rank +88, function_id +92, value +96
		/* EQEmu's GuildUpdateRanks(4) / GuildUpdatePermissions(5) map to client 3 / 4.
		/* The case 0x79d handler does not validate the payload size. */
		struct GuildUpdate_Struct {
			/*000*/	uint32	Action;
			/*004*/	uint8	Gap[84];
			/*088*/	union {
				/* action 0 / 1 */
				char	Text[512];
				/* action 4 */
				struct {
					/*088*/	uint32	Rank;
					/*092*/	uint32	FunctionID;
					/*096*/	uint32	Value;
				} Permission;
				/* action 3 */
				struct {
					/*088*/	uint32	Rank;
					/*092*/	char	RankName[420];
				} RankName;
			} Payload;
		};

		/* Laurion action values for OP_GuildUpdate (0x79d) */
		static const uint32 GuildUpdateActionURL        = 0;
		static const uint32 GuildUpdateActionChannel    = 1;
		static const uint32 GuildUpdateActionRankName   = 3;
		static const uint32 GuildUpdateActionPermission = 4;

		/* 0x7021 OP_GuildMemberAdd - FUN_1400c1250 case 0x7021 (live 0x1400c21ef).
		Allocates a GuildMember (0x3B0 bytes) and fills it from this record:
		  record + 0x08 -> member + 0x58 (level)
		  record + 0x0C -> member + 0x60 (class)
		  record + 0x10 -> member + 0x64 (rank)
		  record + 0x14 -> member + 0x198 (bGuildShowSprite)
		  record + 0x18 -> member + 0x0C (zone/instance, nonzero sets bOnline)
		  record + 0x1C -> member + 0x188 (last login)
		  record + 0x20 -> member + 0x10 (name, min(40, strlen + 1))
		The +000 guid is not read by this case but is what the other guild cases key on. */
		struct GuildMemberAdd_Struct {
			/*000*/	uint64	Guid;             /* EqGuid: u32 char_id + u32 0 */
			/*008*/	uint32	Level;
			/*00C*/	uint32	Class;
			/*010*/	uint32	Rank;
			/*014*/	uint32	GuildShow;
			/*018*/	uint32	ZoneID;
			/*01C*/	uint32	LastOn;
			/*020*/	char	Name[40];
			/*028*/
		};

		/* 0x43f7 OP_GuildMemberList - FUN_1400c1250 case 0x43f7 (live 0x1400c1d64), which runs
		FUN_1400c2b00. This is the record that actually builds the guild window: it sets
		CGuild::GuildGuid (__Guilds + 0x18), CGuild + 0x2D1 and allocates count GuildMember
		objects. Fields are read in this order, so the payload must be written exactly like this:
		  u32 field_length + bytes      <- guild name (read into a string, not stored)
		  u64 GuildGuid                 <- raw, no length prefix
		  u8  flag                      <- raw
		  u32 count                     <- raw
		  per member: u32 field_length + bytes (name, min(40, len + 1) into member + 0x16), then raw:
		    u32 -> member + 0x58 Level
		    u32 -> member + 0x5C Flags (1 = banker, 2 = alt)
		    u32 -> member + 0x60 Class
		    u32 -> member + 0x64 Rank
		    u32 -> member + 0x188 LastLoginTime
		    u8  -> member + 0x3A8
		    u8  -> member + 0x3A9
		    u32 -> member + 0x19C TributeDonations
		    u32 -> member + 0x1A0 LastDonation
		    u8  -> member + 0x14 bMainProfile
		EQEmu's legacy Internal_GuildMembers_Struct (char player_name[64] at +0) does not match. */
		struct GuildMemberList_Struct {
			/*000*/	uint32	NameLength;
			/*004*/	char	Name[64];           /* variable, NameLength bytes */
			/*044*/	uint64	GuildGuid;          /* EqGuid: u32 guild_id + u32 0 */
			/*04C*/	uint8	Flag;
			/*04D*/	uint32	Count;
			/*051*/
		};


		/* 0x20aa OP_GuildMemberUpdate - FUN_1400c1250 case 0x20AA (live 0x1400c12f6).
		Looks the member up by name (record + 8) and then applies:
		  record + 0x48 -> member + 0x0C (instance); nonzero also sets member + 0x08 bOnline
		  record + 0x4C -> member + 0x188 (last login)
		  record + 0x50 -> member + 0x15 (bOfflineMode)
		EQEmu's GuildMemberUpdate_Struct has a u32 GuildID at +0 and the name at +4, so the
		client reads the name from the wrong place. The guild window hides members with
		bOnline = 0 unless "show offline members" is checked, which is why the roster looked
		empty even though the member object was fully populated. */
		struct GuildMemberUpdate_Struct {
			/*000*/	uint64	GuildGuid;        /* EqGuid: u32 guild_id + u32 0 */
			/*008*/	char	MemberName[64];
			/*048*/	uint32	InstanceID;       /* nonzero sets bOnline */
			/*04C*/	uint32	LastSeen;
			/*050*/	uint32	Flag;
			/*054*/
		};

		/* 0x2abf OP_GuildMemberDelete - payload 72 (FUN_1400c1250 case 0x2ABF -> FUN_1400c27f0).
		   FUN_1400c27f0 looks the member up by the name at record + 8. When the name is not the local
		   player it unlinks the member from CGuild's list and decrements the count. When the name is
		   the local player it clears the whole local guild state (tribute fields, window flags) - but
		   it does not clear PcClient::GuildID at pinstLocalPC + 0x1730, which only the profile packet
		   writes, so the profile has to be resent for the removed player. */
		struct GuildMemberDelete_Struct {
			/*000*/	uint64	GuildGuid;        /* EqGuid: u32 guild_id + u32 0 */
			/*008*/	char	MemberName[64];
			/*048*/
		};

		/* 0x1212 OP_GuildManageStatus - payload 82 (FUN_1400c1250 case 0x1212).
		   The handler looks the member up by the name at record + 0xC, then writes:
		     record + 0x08 -> member + 0x64 (Rank)
		     record + 0x4C -> member + 0x5C (Flags: bit 0 banker, bit 1 alt)
		     record + 0x50 -> member + 0x198 (bTributeStatus)
		   and refreshes the guild window. When the name is the local player it also marks the
		   rank selector dirty. EQEmu's GuildManageStatus_Struct is 76 bytes and has no flags
		   field, so the client reads the flags past the end of the packet, and its u64 key at +0
		   becomes guild_id | (oldrank << 32). */
		struct GuildManageStatus_Struct {
			/*000*/	uint64	GuildGuid;        /* EqGuid: u32 guild_id + u32 0 */
			/*008*/	uint32	Rank;
			/*00C*/	char	Name[64];
			/*04C*/	uint32	Flags;            /* bit 0 banker, bit 1 alt */
			/*050*/	uint8	TributeStatus;
			/*051*/	uint8	Field51;
			/*052*/
		};

		/* 0x408f OP_GuildSpawnGuildID - payload 12 (FUN_1400c1250 case 0x408f).
		   This is the only record that writes PlayerClient::GuildID (pinstLocalPlayer + 0x208) for a
		   client that is already in zone. The handler reads a u32 spawn id at +0, resolves it through
		   pinstSpawnManager (0 when the id is 0, which means the local player), then stores the u64
		   guild guid at spawn + 0x208 and refreshes the name sprite. GuildWindow (FUN_1403c9730)
		   returns early when +0x208 is 0, so a player who joins mid-session keeps showing the create
		   guild panel until this record is sent. EQEmu has no encoder for it, so the packet is passed
		   through at this exact size. */
		struct GuildSpawnGuildID_Struct {
			/*000*/	uint32	SpawnID;          /* 0 = pinstLocalPlayer */
			/*004*/	uint64	GuildGuid;        /* EqGuid: u32 guild_id + u32 0 */
			/*00C*/
		};

		/* 0x4a13 OP_TributeItem - payload 40 */
		struct TributeItem_Struct {
			/*000*/	uint64	ItemField0;
			/*008*/	uint32	Quantity;
			/*012*/	uint32	Param3;
			/*016*/	uint32	SpawnID;
			/*020*/	uint32	Stale020;         /* never written by the sender */
			/*024*/	uint64	PlayerField208;
			/*032*/	uint64	Stale032;         /* never written by the sender */
			/*040*/
		};

		/* 0x4b97 OP_GroupFollow - payload 64, single null terminated name */
		struct GroupFollow_Struct {
			/*000*/	char	Name[64];
			/*064*/
		};

		/* 0x5b29 OP_LFGuild subcommand 3 - payload 48 */
		struct LFGuild_SearchPlayer_Struct {
			/*000*/	uint32	Command;
			/*004*/	uint32	Stale004;
			/*008*/	uint64	Stale008;
			/*016*/	uint32	Stale016;
			/*020*/	uint32	Stale020;
			/*024*/	uint32	FromLevel;
			/*028*/	uint32	ToLevel;
			/*032*/	uint32	MinAA;
			/*036*/	uint32	Classes;
			/*040*/	uint32	TimeZone;
			/*044*/	uint32	Stale044;
			/*048*/
		};

		/* 0x5b29 OP_LFGuild subcommand 4 - payload 40 */
		struct LFGuild_SearchGuild_Struct {
			/*000*/	uint32	Command;
			/*004*/	uint32	Stale004;
			/*008*/	uint64	Stale008;
			/*016*/	uint32	Stale016;
			/*020*/	uint32	Stale020;
			/*024*/	uint32	Level;
			/*028*/	uint32	AAPoints;
			/*032*/	uint32	Class;
			/*036*/	uint32	TimeZone;
			/*040*/
		};

		/* 0x62fc tribute donation variant - payload 32 */
		struct TributeMoney_Struct {
			/*000*/	uint32	Platinum;
			/*004*/	uint32	TributeMasterID;
			/*008*/	uint32	Stale008;         /* never written by the sender */
			/*012*/	uint32	Stale012;
			/*016*/	uint64	PlayerField208;
			/*024*/	uint64	Stale024;         /* never written by the sender */
			/*032*/
		};

		/* 0x64b0 OP_BecomeCorpse - payload 4 */
		struct BecomeCorpse_Struct {
			/*000*/	uint32	SpawnID;
			/*004*/
		};

		/* 0x6631 OP_SharedTaskMemberChange - payload 12 */
		struct SharedTaskMemberChange_Struct {
			/*000*/	uint32	Stale000;         /* never written by the sender */
			/*004*/	uint32	AddPlayerID;
			/*008*/	uint8	Flag;
			/*009*/	uint8	Stale009[3];      /* never written by the sender */
			/*012*/
		};

		/* 0x6640 OP_GroupMakeLeader - payload 136 */
		struct GroupMakeLeader_Struct {
			/*000*/	uint32	Unknown000;       /* constant 8 in this client */
			/*004*/	char	CurrentLeader[64];
			/*068*/	char	NewLeader[64];
			/*132*/	uint32	Unknown132;
			/*136*/
		};

		/* 0x6939 OP_PetCommands - payload 12 */
		struct PetCommands_Struct {
			/*000*/	uint32	Command;
			/*004*/	uint32	Target;
			/*008*/	uint8	Param4;
			/*009*/	uint8	Param5;
			/*010*/	uint16	Stale010;         /* never written by the sender */
			/*012*/
		};

		/* 0x7365 OP_GMEmoteWorld - payload 512 */
		struct GMEmoteWorld_Struct {
			/*000*/	char	Text[512];
			/*512*/
		};

		/* 0x7410 right click target select - payload 4 */
		struct SetTargetRightClick_Struct {
			/*000*/	uint32	NewTarget;
			/*004*/
		};

		/* 0x639c OP_TargetHoTT - payload 4 */
		// Client handler ZonePacket__dispatchRecv branch 0x1401d9449 writes
		// [pinstLocalPC + 0x2850 + 0xF88] (MQ2 PlayerClient::TargetOfTarget) from payload dword 0.
		// This is the only server->client path that populates TargetOfTarget. The old value 0x4ec4 is
		// the client's own outbound HoTT report (FUN_1404e8580 / FUN_1404e95b0) and has no receive
		// handler, so EQEmu sending 0x4ec4 is ignored by the client.
		struct TargetHoTT_Struct {
			/*000*/	uint32	TargetOfTarget;
			/*004*/
		};

		/* 0x5b2d OP_PetHoTT - payload 4 */
		// Client handler ZonePacket__dispatchRecv branch 0x1401d7274: payload dword 0 is the pet's target
		// spawn id. It resolves the pet object from [pinstLocalPlayer + 0x630], then writes
		// [pet_spawn + 0xF58] (the pet's Target pointer) with GetSpawnByID(payload dword 0), or clears
		// +0xF58 when payload dword 0 is 0. __GetGaugeValueFromEQ case 0x29 (GAUGETYPE_TARGET_OF_PET)
		// reads that same +0xF58, so this packet is what the Pet Info window's target gauge is fed by.
		// The xtarget slot (type 24 MyPetTarget) only drives the xtarget list, not this window.
		struct PetHoTT_Struct {
			/*000*/	uint32	PetTarget;
			/*004*/
		};

		/* 0x5479 OP_AggroMeterTargetInfo - payload 8 */
		// Client handler FUN_1400ac650: payload[0] -> AggroMeterManagerClient + 0x1E0 (AggroLockID),
		// payload[4] -> + 0x1E4 (AggroTargetID).
		struct AggroMeterTargetInfo_Struct {
			/*000*/	uint32	LockID;
			/*004*/	uint32	TargetID;
			/*008*/
		};

		/* 0x68eb OP_AggroMeterUpdate - variable length */
		// Client handler FUN_1400ac650 reads the buffer byte by byte, so the field order below is the
		// wire order, not a packed struct:
		//   u8 flag; if flag != 0 -> u32 secondary_id (-> AggroMeterManagerClient + 0x1E8)
		//   u8 count; then count x AggroMeterUpdateEntry_Struct
		// Each entry writes a u16 at AggroMeterManagerClient + 8 + type * 0x10, and the client rejects
		// any type > 0x1D, so the aggro type ids must stay inside [0, 29].
		struct AggroMeterUpdateEntry_Struct {
			/*000*/	uint8	Type;
			/*001*/	uint16	Percent;
			/*003*/
		};

		/* 0x123c OP_XTargetResponse - variable length */
		// Client handler FUN_14028ff00. Wire order:
		//   u32 max_slots (resizes the list)
		//   u32 count
		//   count x XTargetResponseEntry_Struct, where the name is a null terminated string
		// The client slot record is 0x4C bytes: u32 xTargetType, u32 status, u32 spawn_id, char name[64].
		struct XTargetResponseEntry_Struct {
			/*000*/	uint32	Slot;
			/*004*/	uint8	Status;
			/*005*/	uint32	SpawnID;
			/*009*/	char	Name[64];
		};

		/* 0x5c3e OP_XTargetRequest - variable length (12 + strlen(name) + 1) */
		// Client send site FUN_14028fb90 (xtarget slot setter, reached from __ExecuteCmd and from the
		// 0x0ede receive branch). This is the only packet that carries the slot TYPE, so it is what lets
		// the server know a slot is MyPetTarget (24) - the Pet window reads that slot.
		// The list is pinstLocalPC + 0x2EA0 (pExtendedTargetList), record 0x4C bytes:
		//   u32 xTargetType, u32 status, u32 spawn_id, char name[64].
		// The name is null terminated on the wire, so only the fixed header is a struct here.
		struct XTargetRequest_Struct {
			/*000*/	uint32	Flag;      /* constant 1 */
			/*004*/	uint32	Slot;
			/*008*/	uint32	Type;      /* XTargetType, MQ XTargetTypes 0..26 */
			/*012*/	char	Name[64];  /* null terminated string on the wire */
		};

		/* 0x0ede OP_0x0ede - payload 8, used in both directions */
		// Send site FUN_140229f50 (/xtarget set <slot> <name>) and receive branch 0x1401e02cb share this
		// opcode and layout. The receive handler derives the type from the spawn (2 = PC, 3 = NPC) and
		// calls FUN_14028fb90(), the same slot setter that drives 0x5c3e.
		struct XTargetSlotUpdate_Struct {
			/*000*/	uint32	SpawnID;
			/*004*/	uint32	Slot;
			/*008*/
		};

		/* 0x7545 OP_InspectBuffs - payload 1 */
		struct InspectBuffs_Struct {
			/*000*/	uint8	ShowTargetBuffs;   /* 0 = own buffs, 1 = target buffs */
			/*001*/
		};

		/* 0x77e6 OP_GuildInviteAccept - payload 144.
		Client send sites FUN_1401fa510 and FUN_1402541f0 case 100 (the invite popup notification
		handler) both send 0x92. FUN_1402541f0 sends 9 when the popup is dismissed and the rank
		stored at record + 0x88 when the player clicks accept, so an accept only reaches the server
		as a rank 1..8. EQEmu's GuildInviteAccept_Struct reads guild_id as a u32 at +132, which the
		client leaves zero. */
		struct GuildInviteAccept_Struct {
			/*000*/	char	Inviter[64];
			/*064*/	char	NewMember[64];
			/*128*/	uint32	Response;         /* 9 decline, 10, 11 */
			/*132*/	uint32	Unknown132;
			/*136*/	uint64	GuildID;
			/*144*/
		};

		/* 0x077a task status clear - payload 8 */
		struct TaskStatusClear_Struct {
			/*000*/	uint32	TaskID;
			/*004*/	uint32	ActivityType;
			/*008*/
		};

		/* 0x6404 OP_WhoAllResponse - /who all reply - variable length */
		/* Handler: FUN_140204e70. The 64 byte header matches the base WhoAllReturnStruct. */
		/* Laurion carries one extra dword per player entry: a status bitmask at +4, which
		 * shifts the pid string id to +8 and the name to +12. Everything after the name is in
		 * the same order as the base struct, except for an additional string between Zone and
		 * Class that is only present when bit 9 of the bitmask is set.
		 *   +0  uint32 FormatStringID   eqstr 5022-5025 (player line), 5026/5027 (zone line)
		 *   +4  uint32 Flags            see WhoAllFlags below
		 *   +8  uint32 PidStringID      eqstr 5003 "(USER %1: PID %2)" / 5004 "(USER PID %1)"
		 *   +12 char Name[]             null terminated
		 *       uint32 RankStringID     guild rank / status string id
		 *       char Guild[]            null terminated
		 *       uint32 Field0           base Unknown80[0]
		 *       uint32 Field1           base Unknown80[1]
		 *       uint32 ZoneStringID     base ZoneMSGID, eqstr 5006 "ZONE: %1"
		 *       uint32 Zone             base Zone
		 *       [char Extra[]]          only when Flags & 0x200
		 *       uint32 Class            base Class_
		 *       uint32 Level            base Level
		 *       uint32 Race             base Race
		 *       char Account[]          base Account
		 *       uint32 Field100         base Unknown100
		 */
		struct WhoAllPlayerHead_Struct {
			/*000*/	uint32	FormatStringID;
			/*004*/	uint32	Flags;
			/*008*/	uint32	PidStringID;
			/*012*/	char	Name[1];
		};

		/* Status bitmask bits decoded by FUN_140204e70 */
		static const uint32 WHOAF_AFK      = 0x001;	// eqstr 12311 " AFK "
		static const uint32 WHOAF_LINKDEAD = 0x008;	// eqstr 12313 " <LINKDEAD>"
		static const uint32 WHOAF_TRADER   = 0x010;	// eqstr 12315 " TRADER"
		static const uint32 WHOAF_BUYER    = 0x020;	// eqstr 6056  " BUYER"
		static const uint32 WHOAF_RIP      = 0x040;	// eqstr 12958 " * RIP *"
		static const uint32 WHOAF_EXTRA    = 0x200;	// extra string present in the entry
		static const uint32 WHOAF_OFFLINE  = 0x400;	// eqstr 767   "OFFLINE "

		/* 0x2a09 OP_WhoAllRequest - /who / /who all - payload 168 */
		/* Sender: FUN_140281340, which copies the whole 168 byte stack frame out verbatim. */
		struct WhoAllRequest_Struct {
			/*000*/	char	Name[64];           /* search word, or guild name when LookupType == -6 */
			/*064*/	char	Unknown064[64];     /* zeroed, never written by the sender */
			/*128*/	uint32	Race;               /* -1 = any race */
			/*132*/	uint32	Class;              /* -1 = any class */
			/*136*/	uint32	LevelLow;           /* -1 = any level */
			/*140*/	uint32	LevelHigh;          /* -1 = any level */
			/*144*/	uint32	GmLookup;           /* -1 unspecified, 0 = NONGM, 1 = GM */
			/*148*/	uint32	LookupType;         /* -1 none, -2 friend, -3 LFG, -4 trader, -5 buyer, -6 guild */
			/*152*/	uint64	GuildID;            /* 0 unless LookupType == -6 */
			/*160*/	uint32	Type;               /* matched word slot: 0 = /who, 3 = /who all */
			/*164*/	uint32	Unknown164;         /* zeroed, never written by the sender */
			/*168*/
		};

		// OP_CharacterCreate (0x6a3c), client -> world, 168 byte body.
		// Built by FUN_140380ed0; the sibling send site FUN_1403811b0 (0x6ccc) uses the same layout
		// but fills char_name from the CCharacterCreation window (window + 0x3600).
		// Only fields with direct evidence are named; the rest stay placeholders.
		// Evidence: docs/OP_CharacterCreatePacket.md
		struct CharCreate_Struct {
			/*000*/	char	char_name[64];   // empty on the 0x6a3c path (name comes from OP_ApproveName)
			/*064*/	uint64	unknown064;      // CEverQuest + 0x5f0 at send time
			/*072*/	uint32	gender;          // profile + 0x1544
			/*076*/	uint32	race;            // profile + 0x1548
			/*080*/	uint32	class_;          // profile + 0x154c
			/*084*/	uint32	deity;           // profile + 0x6ea8
			/*088*/	uint32	start_zone;      // profile + 0x15f4 (StartingCity)
			/*092*/	uint32	unknown092;      // appearance byte, pinstLocalPlayer + 0xfc5
			/*096*/	uint32	unknown096;      // appearance byte, pinstLocalPlayer + 0xfca
			/*100*/	uint32	unknown100;      // appearance byte, pinstLocalPlayer + 0xfc6
			/*104*/	uint32	unknown104;      // appearance byte, pinstLocalPlayer + 0xfc9
			/*108*/	uint32	unknown108;      // appearance byte, pinstLocalPlayer + 0xfc4
			/*112*/	uint32	unknown112;      // appearance byte, pinstLocalPlayer + 0xfc7
			/*116*/	uint32	unknown116;      // appearance byte, pinstLocalPlayer + 0xfc8
			/*120*/	uint32	unknown120;      // pinstLocalPlayer + 0x1044
			/*124*/	uint32	unknown124;      // pinstLocalPlayer + 0x1048
			/*128*/	uint32	unknown128;      // pinstLocalPlayer + 0x104c
			/*132*/	uint32	STR;             // CCharacterCreation stat entry 0
			/*136*/	uint32	STA;             // CCharacterCreation stat entry 3
			/*140*/	uint32	AGI;             // CCharacterCreation stat entry 2
			/*144*/	uint32	DEX;             // CCharacterCreation stat entry 1
			/*148*/	uint32	WIS;             // CCharacterCreation stat entry 5
			/*152*/	uint32	INT;             // CCharacterCreation stat entry 4
			/*156*/	uint32	CHA;             // CCharacterCreation stat entry 6
			/*160*/	uint32	unknown160;      // CCharacterCreation + 0x3874 (combo box choice)
			/*164*/	uint32	unknown164;      // CCharacterCreation + 0x3876/0x3877/0x3878 selector
			/*168*/
		};

		// OP_ReadBook (0x51af). CInvSlot__HandleRButtonUp @ 0x1403e9210 builds the request on the
		// stack and sends 0x2018 payload bytes. The client's reader (FUN_1403731e0 / FUN_14046b990)
		// parses the reply in the same shape, so both directions share this layout.
		static const uint32 BOOK_TEXT_SIZE = 8195;

		struct BookRequest_Struct {
			/*000*/	uint32	window;			// 0xFFFFFFFF = new window
			/*004*/	uint32	book_type;		// ItemDefinition::BookType (0 note, 1 book, 2 item info)
			/*008*/	uint32	target_id;
			/*012*/	int16	top_slot;		// ItemGlobalIndex
			/*014*/	int16	sub_slot;		// ItemGlobalIndex
			/*016*/	uint16	aug_index;		// ItemGlobalIndex
			/*018*/	uint8	unknown018;		// zero on the request
			/*019*/	uint8	can_scribe;		// enables the book window Scribe button
			/*020*/	uint8	can_cast;		// enables the book window Cast Spell button
			/*021*/	char	text[BOOK_TEXT_SIZE];	// book name (request) / book text (reply)
			/*8216*/
		};

		// OP_BookButton (0x14d). BookWindow button handler @ 0x14046bbf0 sends 0x12 bytes total:
		// +0 book window +0x710 (top slot + sub slot), +4 book window +0x714 (aug index),
		// +8 target id, +12 padding.
		struct BookButton_Struct {
			/*000*/	int16	top_slot;
			/*002*/	int16	sub_slot;
			/*004*/	uint16	aug_index;
			/*006*/	uint16	unknown006;
			/*008*/	uint32	target_id;
			/*012*/	uint32	unknown012;
			/*016*/
		};

		// OP_ItemVerifyRequest (0x2003). CInvSlot__HandleRButtonUp @ 0x1403e9210 sends the whole
		// InventorySlot_Struct (12 bytes) followed by the target entity id, so the payload is 16 bytes
		// where EQEmu's canonical ItemVerifyRequest_Struct is only 8 (int32 slot + uint32 target).
		// Without a Laurion decode the handler rejects the packet on size and the right-click does nothing.
		struct ItemVerifyRequest_Struct {
			/*000*/	InventorySlot_Struct	inventory_slot;
			/*012*/	uint32	target;		// Target Entity ID
			/*016*/
		};

#pragma pack()

	};	//end namespace structs

	// ---------------------------------------------------------------------------
	// Mercenary record -- docs/mercenary.md 4.1
	//
	// The record shared by 0x59b7 (owned mercenary list) and 0x689c (merchant shop list).
	// The client's reader is FUN_1402b2140: it reads every field sequentially with NO
	// alignment padding and bounds-checks each read against the remaining size. A packed
	// struct cannot express a 1-byte field sitting between 4-byte fields plus a
	// variable-length name, so the record is written and read through a cursor.
	//
	// Wire order, verified field by field against FUN_1402b2140:
	//
	//   0x00 int32  id
	//   0x04 int32  type_string_id      dbstr category 21 (eMercenaryCategory)
	//   0x08 int32  subtype_string_id   dbstr categories 22 and 23 share one id
	//   0x0C int32  initial_cost
	//   0x10 int32  upkeep_cost
	//   0x14 uint8  required_membership_level   <- a BYTE, not a uint32
	//   0x15 int32  alt_initial_cost
	//   0x19 int32  alt_upkeep_cost
	//   0x1D int32  alt_currency_id
	//   0x21 uint8  alt_currency_only
	//   0x22 int32  time_remaining
	//   0x26 int32  stance_group_id
	//   0x2A int32  stance_id
	//   0x2E int32  num_stances
	//   0x32 int32  unsuspended_time    (the client sign-extends it to int64)
	//   0x36 uint8  auto_assist
	//   0x37 int32  type                <- dbstr category 36 role id, Laurion::MercRole (1..5).
	//      FUN_140452450 formats each MMTW_MercListBox row as
	//      "%s - %s%s" with GetString(rec.type, 36) first, so a 0 here is what the player reads as
	//      "Unknown DB String 0-36 - Apprentice" in the available mercenaries list.
	//   0x3B char*  name                null terminated, strlen+1 consumed
	//   ....     num_stances x { int32 stance_id, int32 stance_string_id }
	//
	// Two things that are easy to get wrong, and both fail silently:
	//   * required_membership_level is one byte. EQEmu's RoF MercenaryData_Struct puts a
	//     uint32 Status at 0x14 and has no type field at all; reusing it shifts every later
	//     field by 3 bytes.
	//   * the stance pairs are INTERLEAVED (id, dbstr, id, dbstr), not two runs. The client
	//     loop is param_1[i*2 + 0x22] = read(); param_1[i*2 + 0x23] = read();
	//
	// Fixed prefix = 59 bytes, then the name, then 8 * num_stances.
	// ---------------------------------------------------------------------------
	struct MercenaryRecord {
		int32_t  id;
		int32_t  type_string_id;
		int32_t  subtype_string_id;
		int32_t  initial_cost;
		int32_t  upkeep_cost;
		uint8_t  required_membership_level;
		int32_t  alt_initial_cost;
		int32_t  alt_upkeep_cost;
		int32_t  alt_currency_id;
		uint8_t  alt_currency_only;
		int32_t  time_remaining;
		int32_t  stance_group_id;
		int32_t  stance_id;
		int32_t  num_stances;
		int32_t  unsuspended_time;
		uint8_t  auto_assist;
		int32_t  type;   // Laurion::MercRole -- dbstr category 36 (1 General .. 5 Caster DPS)
		std::string name;
		std::vector<std::pair<int32_t, int32_t>> stances;  // {stance_id, stance dbstr id}

		MercenaryRecord()
		{
			id = 0;
			type_string_id = 0;
			subtype_string_id = 0;
			initial_cost = 0;
			upkeep_cost = 0;
			required_membership_level = 0;
			alt_initial_cost = 0;
			alt_upkeep_cost = 0;
			alt_currency_id = 0;
			alt_currency_only = 0;
			time_remaining = 0;
			stance_group_id = 0;
			stance_id = 0;
			num_stances = 0;
			unsuspended_time = 0;
			auto_assist = 0;
			type = 0;
		}
	};

	// Bytes before the name: 5*4 + 1 + 3*4 + 1 + 4*4 + 1 + 4 = 59.
	static const uint32 MERCENARY_RECORD_FIXED_SIZE = 59;

	// The client's MercenaryInfo holds stanceData as a FIXED array at 0x88..0xD8 (0x50 bytes,
	// 8 per stance). FUN_1402b2140 loops `i < num_stances` and writes param_1[i*2 + 0x22] with
	// no bound check against that array, so a num_stances above 10 writes past the end of the
	// record the client just allocated. 10 is also MAX_MERC_STANCES in MacroQuestLS and the
	// stance dbstr categories (24/25) only define ids 1..9. Never send more than this.
	static const uint32 MERCENARY_MAX_STANCES = 10;

	inline uint32 MercenaryRecordWireSize(const MercenaryRecord &r)
	{
		return MERCENARY_RECORD_FIXED_SIZE
			+ static_cast<uint32>(r.name.size()) + 1
			+ 8u * static_cast<uint32>(r.stances.size());
	}

	// Drop any stances beyond the client's fixed array and keep num_stances in step with the
	// vector, so a record built from a database list can never overflow the client's struct.
	inline void ClampMercenaryRecordStances(MercenaryRecord &r)
	{
		if (r.stances.size() > MERCENARY_MAX_STANCES) {
			r.stances.resize(MERCENARY_MAX_STANCES);
		}
		r.num_stances = static_cast<int32_t>(r.stances.size());
	}

	// Sequential unaligned writer. Mirrors the read order of FUN_1402b2140 exactly.
	// Overflow is latched rather than asserted: an oversized record must not walk off the end
	// of the allocated packet, and the caller checks Ok() before queueing.
	class MercenaryRecordWriter {
	public:
		MercenaryRecordWriter(unsigned char *buffer, uint32 capacity)
			: m_buffer(buffer), m_capacity(capacity), m_offset(0), m_overflow(false) {}

		void Int32(int32_t v) { Put(&v, 4); }
		void UInt8(uint8_t v) { Put(&v, 1); }
		void String(const char *s)
		{
			const uint32 len = static_cast<uint32>(strlen(s)) + 1;   // include the NUL
			Put(s, len);
		}

		uint32 Offset() const { return m_offset; }
		bool Ok() const { return !m_overflow; }

	private:
		void Put(const void *src, uint32 len)
		{
			if (m_offset + len > m_capacity) {
				m_overflow = true;
				return;
			}
			memcpy(m_buffer + m_offset, src, len);
			m_offset += len;
		}

		unsigned char *m_buffer;
		uint32 m_capacity;
		uint32 m_offset;
		bool m_overflow;
	};

	inline void WriteMercenaryRecord(MercenaryRecordWriter &w, const MercenaryRecord &r)
	{
		w.Int32(r.id);
		w.Int32(r.type_string_id);
		w.Int32(r.subtype_string_id);
		w.Int32(r.initial_cost);
		w.Int32(r.upkeep_cost);
		w.UInt8(r.required_membership_level);
		w.Int32(r.alt_initial_cost);
		w.Int32(r.alt_upkeep_cost);
		w.Int32(r.alt_currency_id);
		w.UInt8(r.alt_currency_only);
		w.Int32(r.time_remaining);
		w.Int32(r.stance_group_id);
		w.Int32(r.stance_id);
		w.Int32(r.num_stances);
		w.Int32(r.unsuspended_time);
		w.UInt8(r.auto_assist);
		w.Int32(r.type);
		w.String(r.name.c_str());

		// Interleaved, matching the client's i*2 + 0x22 / i*2 + 0x23 loop.
		for (const auto &s : r.stances) {
			w.Int32(s.first);
			w.Int32(s.second);
		}
	}

	// Sequential unaligned reader for the DECODE side. Bounds-checked the way the client
	// bounds-checks: a truncated record stops early instead of reading past the end.
	class MercenaryRecordReader {
	public:
		MercenaryRecordReader(const unsigned char *buffer, uint32 size)
			: m_buffer(buffer), m_size(size), m_offset(0) {}

		bool Int32(int32_t &v)
		{
			if (m_offset + 4 > m_size) return false;
			memcpy(&v, m_buffer + m_offset, 4);
			m_offset += 4;
			return true;
		}

		bool UInt8(uint8_t &v)
		{
			if (m_offset + 1 > m_size) return false;
			v = m_buffer[m_offset];
			m_offset += 1;
			return true;
		}

		bool String(std::string &s)
		{
			const char *start = reinterpret_cast<const char *>(m_buffer + m_offset);
			const uint32 available = m_size - m_offset;
			const uint32 len = static_cast<uint32>(strnlen(start, available));
			if (m_offset + len + 1 > m_size) return false;   // unterminated
			s.assign(start, len);
			m_offset += len + 1;                              // consume the NUL
			return true;
		}

		uint32 Offset() const { return m_offset; }
		uint32 Remaining() const { return m_size - m_offset; }

	private:
		const unsigned char *m_buffer;
		uint32 m_size;
		uint32 m_offset;
	};

	inline bool ReadMercenaryRecord(MercenaryRecordReader &r, MercenaryRecord &out)
	{
		if (!r.Int32(out.id)) return false;
		if (!r.Int32(out.type_string_id)) return false;
		if (!r.Int32(out.subtype_string_id)) return false;
		if (!r.Int32(out.initial_cost)) return false;
		if (!r.Int32(out.upkeep_cost)) return false;
		if (!r.UInt8(out.required_membership_level)) return false;
		if (!r.Int32(out.alt_initial_cost)) return false;
		if (!r.Int32(out.alt_upkeep_cost)) return false;
		if (!r.Int32(out.alt_currency_id)) return false;
		if (!r.UInt8(out.alt_currency_only)) return false;
		if (!r.Int32(out.time_remaining)) return false;
		if (!r.Int32(out.stance_group_id)) return false;
		if (!r.Int32(out.stance_id)) return false;
		if (!r.Int32(out.num_stances)) return false;
		if (!r.Int32(out.unsuspended_time)) return false;
		if (!r.UInt8(out.auto_assist)) return false;
		if (!r.Int32(out.type)) return false;
		if (!r.String(out.name)) return false;

		// num_stances comes from an untrusted buffer, and the client itself does not bound it.
		// Reject anything the client's fixed stanceData array could not hold.
		const int32_t count = out.num_stances;
		if (count < 0 || static_cast<uint32>(count) > MERCENARY_MAX_STANCES) {
			return false;
		}

		out.stances.clear();
		out.stances.reserve(static_cast<size_t>(count));
		for (int32_t i = 0; i < count; ++i) {
			std::pair<int32_t, int32_t> s{};
			if (!r.Int32(s.first)) return false;
			if (!r.Int32(s.second)) return false;
			out.stances.push_back(s);
		}

		return true;
	}

	// Mercenary AA ability definition record, opcode 0x78e8.
	//
	// Parsed by FUN_1401a7090, which reads ten u32 fields and then calls FUN_1401a6ea0 for the
	// {lo, hi} array tail. The 0x78e8 arm of FUN_1401a5fa0 copies the parsed record into a node of
	// the definition container at mgr+0x48 (buckets at mgr+0x68, hashed by ability_id & 0x3f), so
	// these offsets map 1:1 onto what every later accessor reads back.
	//
	// The meanings come from the two places that actually consume them.
	//
	// FUN_14033cd80 builds one cell of AAW_MercAAList from the node payload (call it f[]):
	//   column 0  GetString(f[1], 0x25)                            -> category 37, ability name
	//   column 1  "%d/%d" from f[5] and GetMercMaxRank             -> f[5] is the rank of this ability
	//   column 2  DAT_14081c65c formatted with f[3]                -> the cost column
	//   column 3  f[6] > 0 ? GetString(f[6], 0x24) : empty string  -> category 36, ability type
	// A cell whose ability fails FUN_1401a53f0 is drawn in 0xffc0c0c0 (grey).
	//
	// FUN_1401a53f0 is the buy gate, same node layout:
	//   (int)f[3] <= *(int *)(pinstLocalPC + 0x2738)  -> cost against the AA point pool, which
	//                                                  0x50e6 action 1 writes from its payload
	//   (int)f[7] <= <player level getter, vtable + 0xe8> -> level requirement
	//   FUN_1401251e0(mgr + 0x488, f[8])              -> alternate currency container lookup
	//   a prerequisite needs (int)prereq_rank <= (int)f[5] of this ability
	//
	// f[3] is the purchase cost, not a rank count. The maximum rank the client enforces comes from
	// CAltAbilityData__GetMercMaxRank, which returns the member count of the group FUN_1401a4ff0
	// builds from f[4]; f[3] never feeds it.
	struct MercenaryAAAbilityRecord {
		int32_t ability_id = 0;   // f0  [PROVEN] hash key, BuyAbility argument, first u32 of 0x3861
		int32_t name_sid = 0;     // f1  [PROVEN] dbstr category 37 eMercenaryAbilityName
		int32_t desc_sid = 0;     // f2  [CANDIDATE] dbstr category 38 eMercenaryAbilityDescription
		int32_t cost = 0;         // f3  [PROVEN] AA point cost: cost column and the FUN_1401a53f0 gate
		int32_t group_id = 0;     // f4  [PROVEN] key of the granted-ability container (mgr+0x6d0)
		int32_t rank_index = 0;   // f5  [PROVEN] rank this ability represents inside group_id
		int32_t aa_type = 0;      // f6  [PROVEN] dbstr category 36 eMercenaryAbilityType (1..5)
		int32_t level_req = 0;    // f7  [PROVEN] minimum player level, compared against the PC level
		int32_t currency_id = 0;  // f8  [PROVEN] alternate currency id checked at mgr+0x488
		int32_t flags = 0;        // f9  unresolved: no consumer found, sent as zero

		// FUN_1401a6ea0 reads array_count, then array_count pairs. Each pair becomes one u64 entry
		// with the first u32 in the low half, so entry.first is the u32 that appears first on the wire.
		std::vector<std::pair<int32_t, int32_t>> ranks;
	};

	// Ten fields + the array_count dword.
	static const uint32 MERCENARY_AA_RECORD_FIXED_SIZE = 44;

	// The client clears both the definition container (mgr+0x48) and the group index (mgr+0x268) at
	// the top of the 0x78e8 arm -- FUN_1401a6ce0 and FUN_1401a6c40 -- so this packet is a full
	// replacement and re-sending it on every zone-in is correct rather than duplicating rows.
	inline uint32 MercenaryAARecordWireSize(const MercenaryAAAbilityRecord &r)
	{
		return MERCENARY_AA_RECORD_FIXED_SIZE
			+ static_cast<uint32>(r.ranks.size()) * 8;
	}

	inline void WriteMercenaryAAAbility(MercenaryRecordWriter &w, const MercenaryAAAbilityRecord &r)
	{
		w.Int32(r.ability_id);
		w.Int32(r.name_sid);
		w.Int32(r.desc_sid);
		w.Int32(r.cost);
		w.Int32(r.group_id);
		w.Int32(r.rank_index);
		w.Int32(r.aa_type);
		w.Int32(r.level_req);
		w.Int32(r.currency_id);
		w.Int32(r.flags);

		w.Int32(static_cast<int32_t>(r.ranks.size()));
		for (const auto &p : r.ranks) {
			w.Int32(p.first);
			w.Int32(p.second);
		}
	}

	inline bool ReadMercenaryAAAbility(MercenaryRecordReader &r, MercenaryAAAbilityRecord &out)
	{
		if (!r.Int32(out.ability_id)) return false;
		if (!r.Int32(out.name_sid)) return false;
		if (!r.Int32(out.desc_sid)) return false;
		if (!r.Int32(out.cost)) return false;
		if (!r.Int32(out.group_id)) return false;
		if (!r.Int32(out.rank_index)) return false;
		if (!r.Int32(out.aa_type)) return false;
		if (!r.Int32(out.level_req)) return false;
		if (!r.Int32(out.currency_id)) return false;
		if (!r.Int32(out.flags)) return false;

		int32_t count = 0;
		if (!r.Int32(count)) {
			return false;
		}
		// Each entry is 8 bytes, so a count larger than the remaining buffer cannot be satisfied.
		// Reject it instead of looping against the bounds checks.
		if (count < 0 || static_cast<uint32>(count) > (r.Remaining() / 8)) {
			return false;
		}

		out.ranks.clear();
		out.ranks.reserve(static_cast<size_t>(count));
		for (int32_t i = 0; i < count; ++i) {
			std::pair<int32_t, int32_t> p{};
			if (!r.Int32(p.first)) return false;
			if (!r.Int32(p.second)) return false;
			out.ranks.push_back(p);
		}

		return true;
	}

};	//end namespace laurion

#endif /*LAURION_STRUCTS_H_*/
