// Copyright (C) 2004 WoW Daemon
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

#include "Common.h"
#include "Database/DatabaseEnv.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "Opcodes.h"
#include "Log.h"
#include "World.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "UpdateMask.h"
#include "Chat.h"


void WorldSession::HandleCharEnumOpcode( WorldPacket & recv_data )
{
    WorldPacket data;

    // parse m_characters and build a mighty packet of
    // characters to send to the client.
    data.Initialize(SMSG_CHAR_ENUM);

    // loading characters
    std::stringstream ss;
    ss << "SELECT guid FROM characters WHERE acct=" << GetAccountId();

    QueryResult* result = sDatabase.Query( ss.str().c_str() );
    uint8 num = 0;

    data << num;

    if( result )
    {
        Player *plr;
        do
        {
            plr = new Player;
            ASSERT(plr);

            plr->LoadFromDB( (*result)[0].GetUInt32() );
            plr->BuildEnumData( &data );

            delete plr;

            num++;
        }
        while( result->NextRow() );

        delete result;
    }

    data.put<uint8>(0, num);

    SendPacket( &data );
}


void WorldSession::HandleCharCreateOpcode( WorldPacket & recv_data )
{
    std::string name;
    WorldPacket data;

    recv_data >> name;
    recv_data.rpos(0);

    std::stringstream ss;
    ss << "SELECT guid FROM characters WHERE name = '" << name << "'";

    QueryResult *result = sDatabase.Query( ss.str( ).c_str( ) );
    if (result)
    {
        delete result;

        data.Initialize(SMSG_CHAR_CREATE);
        data << (uint8)0x30; // Error codes below
        SendPacket( &data );

        return;
    }

    // loading characters
    ss.rdbuf()->str("");
    ss << "SELECT guid FROM characters WHERE acct=" << GetAccountId();
    result = sDatabase.Query( ss.str( ).c_str( ) );
    if (result)
    {
        if (result->GetRowCount() >= 10)
        {
            data.Initialize(SMSG_CHAR_CREATE);
            data << (uint8)0x2F; // Should be a better error code i think
            SendPacket( &data );
            delete result;
            return;
        }
        delete result;
    }

    Player * pNewChar = new Player;
    pNewChar->Create( objmgr.GenerateLowGuid(HIGHGUID_PLAYER), recv_data );
    pNewChar->SetSession(this); // we need account id
    pNewChar->SaveToDB();

    delete pNewChar;

    data.Initialize( SMSG_CHAR_CREATE );
    data << (uint8)0x2D; // Error codes below
    SendPacket( &data );
}

/*
SMSG_CHAR_CREATE Error Codes:
0x01 Failure
0x02 Canceled
0x03 Disconnect from server
0x04 Failed to connect
0x05 Connected
0x06 Wrong client version
0x07 Connecting to server
0x08 Negotiating security
0x09 Negotiating security complete
0x0A Negotiating security complete
0x0B Authenticating
0x0C Authentication successful
0x0D Authentication failed
0x0E Login unavailable - Please contact Tech Support
0x0F Server is not valid
0x10 System unavailable
0x11 System error
0x12 Billing system error
0x13 Account billing has expired
0x14 Wrong client version
0x15 Unknown account
0x16 Incorrect password
0x17 Session expired
0x18 Server Shutting Down
0x19 Already logged in
0x1A Invalid login server
0x1B Position in Queue: 0
0x1C This account has been banned
0x1D This character is still logged on
0x1E Your WoW subscription has expired
0x1F This session has timed out
0x20 This account has been temporarily suspended
0x21 Retrieving realmlist
0x22 Realmlist retrieved
0x23 Unable to connect to realmlist server
0x24 Invalid realmlist
0x25 The game server is currently down
0x26 Creating account
0x27 Account created
0x28 Account creation failed
0x29 Retrieving character list
0x2A Character list retrieved
0x2B Error retrieving character list
0x2C Creating character
0x2D Character created
0x2E Error creating character
0x2F Character creation failed
0x30 Name already in use
0x31 Creation of that race/class is disabled
0x32 All characters on a PVP realm must be on the same team
0x33 Deleting character
0x34 Character deleted
0x35 Character deletion failed
0x36 Entering the WoW
0x37 Login successful
0x38 World server down
0x39 A character with that name already exists
0x3A No instance server available
0x3B Login failed
0x3C Login for that race/class is disabled
0x3D Enter a name for your character
0x3E Names must be atleast 2 characters long
0x3F Names must be no more then 12 characters
0x40 Names can only contain letters
0x41 Names must contain only one language
0x42 That name contains profanity
0x43 That name is reserved
0x44 You cannot use an apostrophe
0x45 You can only have one apostrophe
0x46 You cannot use the same letter three times consecutively
0x47 Invalid character name
0x48 <Blank>
All further codes give the number in dec.
*/

void WorldSession::HandleCharDeleteOpcode( WorldPacket & recv_data )
{
    WorldPacket data;

    uint64 guid;
    recv_data >> guid;

    Player* plr = new Player;
    ASSERT(plr);

    plr->LoadFromDB( GUID_LOPART(guid) );
    plr->DeleteFromDB();

    delete plr;

    data.Initialize(SMSG_CHAR_CREATE);
    data << (uint8)0x34;
    SendPacket( &data );
}


void WorldSession::HandlePlayerLoginOpcode( WorldPacket & recv_data )
{
    WorldPacket data;
    uint64 playerGuid = 0;

    Log::getSingleton( ).outDebug( "WORLD: Recvd Player Logon Message" );

    recv_data >> playerGuid; // this is the GUID selected by the player

    Player* plr = new Player;
    ASSERT(plr);

    plr->LoadFromDB(GUID_LOPART(playerGuid));
    plr->SetSession(this);
    SetPlayer(plr);

    data.Initialize( SMSG_ACCOUNT_DATA_MD5 );
    for(int i = 0; i < 80; i++)
        data << uint8(0);

    SendPacket(&data);

    // MOTD
    sChatHandler.FillSystemMessageData(&data, this, sWorld.GetMotd());
    SendPacket( &data );

    Log::getSingleton( ).outDebug( "WORLD: Sent motd (SMSG_MESSAGECHAT)" );

    //data.Initialize(4, SMSG_SET_REST_START);
    //data << unsure;
    //SendPacket(&data);

    //Tutorial Flags
    data.Initialize( SMSG_TUTORIAL_FLAGS );
    for(int i = 0; i < 32; i++)
        data << uint8(0xFF);
    SendPacket(&data);
    Log::getSingleton( ).outDebug( "WORLD: Sent tutorial flags." );

    //Initial Spells
    GetPlayer()->smsg_InitialSpells();

    // SMSG_ACTION_BUTTONS
    data.Initialize(SMSG_ACTION_BUTTONS);
    data << uint32(0x19CB);
    data << uint32(0x0074);
    data << uint32(0x0085);
    data << uint32(0x0848);
    for(int i = 0; i < 116; i++)
        data << uint32(0);
    SendPacket( &data );

    // SMSG_INITIALIZE_FACTIONS

    // SMSG_EXPLORATION_EXPERIENCE

    // SMSG_CAST_RESULT -- Spell_id = 836 (LOGINEFFECT (24347)) From spells.dbc.csv

    data.Initialize(SMSG_LOGIN_SETTIMESPEED);
    time_t minutes = sWorld.GetGameTime( ) / 60;
    time_t hours = minutes / 60; minutes %= 60;
    time_t gameTime = minutes + ( hours << 6 );
    data << (uint32)gameTime;
    data << (float)0.017f;  // Normal Game Speed
    SendPacket( &data );

    // SMSG_UPDATE_AURA_DURATION -- if the player had an aura on when he logged out

    // Bojangles has Been up in here :0 Cinematics working Just need
    // the sound flags to kick off sound.
    // doesnt check yet if its the first login to run. *YET*
    // WantedMan fixed so it will only start if you are at starting loc

    data.Initialize( SMSG_TRIGGER_CINEMATIC );
    uint8 theRace = GetPlayer()->getRace(); // get race

    PlayerCreateInfo *info = objmgr.GetPlayerCreateInfo(theRace, 1);
    ASSERT(info);

    if (theRace == 1) // Human
    {
        if (GetPlayer()->m_positionX == info->positionX)
        if (GetPlayer()->m_positionY == info->positionY)
        if (GetPlayer()->m_positionZ == info->positionZ)
        {
            data << uint32(81);
            SendPacket( &data );
        }
    }

    if (theRace == 2) // Orc
    {
        if (GetPlayer()->m_positionX == info->positionX)
        if (GetPlayer()->m_positionY == info->positionY)
        if (GetPlayer()->m_positionZ == info->positionZ)
        {
            data << uint32(21);
            SendPacket( &data );
        }
    }

    if (theRace == 3) // Dwarf
    {
        if (GetPlayer()->m_positionX == info->positionX)
        if (GetPlayer()->m_positionY == info->positionY)
        if (GetPlayer()->m_positionZ == info->positionZ)
        {
            data << uint32(41);
            SendPacket( &data );
        }
    }
    if (theRace == 4) // Night Elves
    {
        if (GetPlayer()->m_positionX == info->positionX)
        if (GetPlayer()->m_positionY == info->positionY)
        if (GetPlayer()->m_positionZ == info->positionZ)
        {
            data << uint32(61);
            SendPacket( &data );
        }
    }
    if (theRace == 5) // undead <-- WORKING thats Correct
    {
        if (GetPlayer()->m_positionX == info->positionX)
        if (GetPlayer()->m_positionY == info->positionY)
        if (GetPlayer()->m_positionZ == info->positionZ)
        {
            data << uint32(2);
            SendPacket( &data );
        }
    }
    if (theRace == 6) // Tauren
    {
        if (GetPlayer()->m_positionX == info->positionX)
        if (GetPlayer()->m_positionY == info->positionY)
        if (GetPlayer()->m_positionZ == info->positionZ)
        {
            data << uint32(141);
            SendPacket( &data );
        }
    }
    if (theRace == 7) // Gnome
    {
        if (GetPlayer()->m_positionX == info->positionX)
        if (GetPlayer()->m_positionY == info->positionY)
        if (GetPlayer()->m_positionZ == info->positionZ)
        {
            data << uint32(101);
            SendPacket( &data );
        }
    }
    if (theRace == 8) // Troll
    {
        if (GetPlayer()->m_positionX == info->positionX)
        if (GetPlayer()->m_positionY == info->positionY)
        if (GetPlayer()->m_positionZ == info->positionZ)
        {
            data << uint32(121);
            SendPacket( &data );
        }
    }

    Player *pCurrChar = GetPlayer();

    // Now send all A9's
    // Add character to the ingame list
    // Build the in-range set
    // Send a message to other clients that a new player has entered the world
    // And let this client know we're in game

    objmgr.AddObject( pCurrChar );
    pCurrChar->PlaceOnMap();

    Log::getSingleton( ).outDetail( "WORLD: Created new player for existing players (%s)", pCurrChar->GetName() );

    std::string outstring = pCurrChar->GetName();
    outstring.append( " has entered the world." );
    sWorld.SendWorldText( outstring.c_str( ) );
}
