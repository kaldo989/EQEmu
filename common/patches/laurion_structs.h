#ifndef LAURION_STRUCTS_H_
#define LAURION_STRUCTS_H_

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
			/*013*/ int32 entries[33]; // Most -1, 1, and 0 for Gold Status
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
		** Wire order verified from FUN_140640960 (bounds-guarded stream reader):
		**   [uint8 flag]            -> FreeToPlay +0x00
		**   [uint32]                -> FreeToPlay +0x04 (class bitmask, tested by the 0x832 handler)
		**   [uint32]                -> FreeToPlay +0x08 (race bitmask, tested by the 0x832 handler)
		**   [uint32 count][count x uint32] -> FreeToPlay +0x0C + i*4 (capped at 32)
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
			GuildID,
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
			/*004*/	uint8	unknown004; // seen 0
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

		/* 0x2d05 OP_GuildInvite and 0x6313 OP_GuildRemove - payload 144 */
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

		/* 0x77e6 OP_GuildInviteAccept - payload 144 */
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

#pragma pack()

	};	//end namespace structs
};	//end namespace laurion

#endif /*LAURION_STRUCTS_H_*/
