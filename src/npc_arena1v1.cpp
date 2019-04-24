/*
*
* Copyright (C) 2013 Emu-Devstore <http://emu-devstore.com/>
* Written by Teiby <http://www.teiby.de/>
* Adjusted by fr4z3n for azerothcore
* Reworked by XDev
*/

#include "ArenaTeamMgr.h"
#include "Common.h"
#include "DisableMgr.h"
#include "BattlegroundMgr.h"
#include "Battleground.h"
#include "ArenaTeam.h"
#include "Language.h"
#include "Configuration/Config.h"
#include "ScriptMgr.h"
#include "npc_arena1v1.h"
#include "Log.h"
#include "Player.h"

class arena1v1_worldscript : public WorldScript
{
public:
    arena1v1_worldscript() : WorldScript("arena1v1_worldscript") { }

    void OnBeforeConfigLoad(bool reload) override
    {
        if (!reload) {
            std::string conf_path = _CONF_DIR;
            std::string cfg_file = conf_path + "/1v1arena.conf";
            std::string cfg_def_file = cfg_file + ".dist";
            sConfigMgr->LoadMore(cfg_def_file.c_str());
            sConfigMgr->LoadMore(cfg_file.c_str());
			
        }
    }
};

class arena1v1announce : public PlayerScript{
public:

    arena1v1announce() : PlayerScript("arena1v1announce") { }

void OnLogin(Player* pPlayer) override
    {
        if (sConfigMgr->GetBoolDefault("Arena1v1Announcer.Enable", true))
        {
            ChatHandler(pPlayer->GetSession()).SendSysMessage("This server is running the |cff4CFF00Arena 1v1 |rmodule.");
        }
    }
};

class npc_1v1arena : public CreatureScript
{
public:
    npc_1v1arena() : CreatureScript("npc_1v1arena") {}


    bool JoinQueueArena(Player* player, Creature* me, bool isRated)
    {

        if (!player || !me)
            return false;

        if (sConfigMgr->GetIntDefault("Arena1v1MinLevel", 70) > player->getLevel())
            return false;

        uint64 guid = player->GetGUID();
        uint8 arenaslot = ArenaTeam::GetSlotByType(ARENA_TEAM_5v5);
        uint8 arenatype = ARENA_TYPE_5v5;
        uint32 arenaRating = 0;
        uint32 matchmakerRating = 0;

        // ignore if we already in BG or BG queue
        if (player->InBattleground())
            return false;

        //check existance
        Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(BATTLEGROUND_AA);
        if (!bg)
        {
            sLog->outString("Battleground: template bg (all arenas) not found");
            return false;
        }

        if (DisableMgr::IsDisabledFor(DISABLE_TYPE_BATTLEGROUND, BATTLEGROUND_AA, NULL))
        {
            ChatHandler(player->GetSession()).PSendSysMessage(LANG_ARENA_DISABLED);
            return false;
        }

        BattlegroundTypeId bgTypeId = bg->GetBgTypeID();
        BattlegroundQueueTypeId bgQueueTypeId = BattlegroundMgr::BGQueueTypeId(bgTypeId, arenatype);
        PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(bg->GetMapId(), player->getLevel());
        if (!bracketEntry)
            return false;

        GroupJoinBattlegroundResult err = ERR_GROUP_JOIN_BATTLEGROUND_FAIL;

        // check if already in queue
        if (player->GetBattlegroundQueueIndex(bgQueueTypeId) < PLAYER_MAX_BATTLEGROUND_QUEUES)
            //player is already in this queue
            return false;
        // check if has free queue slots
        if (!player->HasFreeBattlegroundQueueId())
            return false;

        uint32 ateamId = 0;

        if (isRated)
        {
            ateamId = player->GetArenaTeamId(arenaslot);
            ArenaTeam* at = sArenaTeamMgr->GetArenaTeamById(ateamId);
            if (!at)
            {
                player->GetSession()->SendNotInArenaTeamPacket(arenatype);
                return false;
            }

            // get the team rating for queueing
            arenaRating = at->GetRating();
            matchmakerRating = arenaRating;
            // the arenateam id must match for everyone in the group

            if (arenaRating <= 0)
                arenaRating = 1;
        }

        BattlegroundQueue &bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
        bg->SetRated(isRated);

        GroupQueueInfo* ginfo = bgQueue.AddGroup(player, NULL, bracketEntry, isRated, false, arenaRating, matchmakerRating, ateamId);
        uint32 avgTime = bgQueue.GetAverageQueueWaitTime(ginfo);
        uint32 queueSlot = player->AddBattlegroundQueueId(bgQueueTypeId);

        WorldPacket data;
        // send status packet (in queue)
        sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, queueSlot, STATUS_WAIT_QUEUE, avgTime, 0, arenatype, TEAM_NEUTRAL, true);
        player->GetSession()->SendPacket(&data);

        //sBattlegroundMgr->ScheduleQueueUpdate(matchmakerRating, arenatype, bgQueueTypeId, bgTypeId, bracketEntry->GetBracketId());

        return true;
    }
   

    bool CreateArenateam(Player* player, Creature* me)
    {
        if (!player || !me)
            return false;

        uint8 slot = ArenaTeam::GetSlotByType(ARENA_TEAM_5v5);
        if (slot >= MAX_ARENA_SLOT)
            return false;

        // Check if player is already in an arena team
        if (player->GetArenaTeamId(slot))
        {
            player->GetSession()->SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, player->GetName(), "", ERR_ALREADY_IN_ARENA_TEAM);
            return false;
        }


        // Teamname = playername
        // if teamname exist, we have to choose another name (playername + number)
        int i = 1;
        std::stringstream teamName;
        teamName << player->GetName();
        do
        {
            if (sArenaTeamMgr->GetArenaTeamByName(teamName.str()) != NULL) // teamname exist, so choose another name
            {
                teamName.str(std::string());
                teamName << player->GetName() << (i++);
            }
            else
                break;
        } while (i < 100); // should never happen

                           // Create arena team
        ArenaTeam* arenaTeam = new ArenaTeam();

        if (!arenaTeam->Create(player->GetGUID(), ARENA_TEAM_5v5, teamName.str(), 4283124816, 45, 4294242303, 5, 4294705149))
        {
            delete arenaTeam;
            return false;
        }

        // Register arena team
        sArenaTeamMgr->AddArenaTeam(arenaTeam);
        arenaTeam->AddMember(player->GetGUID());

        ChatHandler(player->GetSession()).SendSysMessage("1v1 Arenateam successfully created!");

        return true;
    }


    bool OnGossipHello(Player* player, Creature* me)
    {
        if (!player || !me)
            return true;

        if (sConfigMgr->GetBoolDefault("Arena1v1.Enable", true) == false)
        {
            ChatHandler(player->GetSession()).SendSysMessage("1v1 disabled!");
            return true;
        }

        if (player->InBattlegroundQueueForBattlegroundQueueType(BATTLEGROUND_QUEUE_5v5))
            player->ADD_GOSSIP_ITEM_EXTENDED(GOSSIP_ICON_CHAT, "Queue leave 1v1 Arena", GOSSIP_SENDER_MAIN, 3, "Are you sure?", 0, false);
        else
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "Queue enter 1v1 Arena(UnRated)", GOSSIP_SENDER_MAIN, 20);

        if (player->GetArenaTeamId(ArenaTeam::GetSlotByType(ARENA_TEAM_5v5)) == 0)
            player->ADD_GOSSIP_ITEM_EXTENDED(GOSSIP_ICON_CHAT, "Create new 1v1 Arena Team", GOSSIP_SENDER_MAIN, 1, "Are you sure?", sConfigMgr->GetIntDefault("Arena1v1Costs", 400000), false);
        else
        {
            if (player->InBattlegroundQueueForBattlegroundQueueType(BATTLEGROUND_QUEUE_5v5) == false)
            {
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "Queue enter 1v1 Arena (Rated)", GOSSIP_SENDER_MAIN, 2);
                player->ADD_GOSSIP_ITEM_EXTENDED(GOSSIP_ICON_CHAT, "Arenateam Clear", GOSSIP_SENDER_MAIN, 5, "Are you sure?", 0, false);
            }
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "Shows your statistics", GOSSIP_SENDER_MAIN, 4);
        }

        /*player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "Script Info", GOSSIP_SENDER_MAIN, 8);*/
        player->SEND_GOSSIP_MENU(68, me->GetGUID());
        return true;
    }



    bool OnGossipSelect(Player* player, Creature* me, uint32 /*uiSender*/, uint32 uiAction)
    {
        if (!player || !me)
            return true;

        player->PlayerTalkClass->ClearMenus();

        switch (uiAction)
        {
        case 1: // Create new Arenateam
        {
            if (sConfigMgr->GetIntDefault("Arena1v1MinLevel", 80) <= player->getLevel())
            {
                if (player->GetMoney() >= uint32(sConfigMgr->GetIntDefault("Arena1v1Costs", 400000)) && CreateArenateam(player, me))
                    player->ModifyMoney(sConfigMgr->GetIntDefault("Arena1v1Costs", 400000) * -1);
            }
            else
            {
                ChatHandler(player->GetSession()).PSendSysMessage("You have to be level %u + to create a 1v1 arena team.", sConfigMgr->GetIntDefault("Arena1v1MinLevel", 70));
                player->CLOSE_GOSSIP_MENU();
                return true;
            }
        }
        break;

        case 2: // Join Queue Arena (rated)
        {
            if (Arena1v1CheckTalents(player) && JoinQueueArena(player, me, true) == false)
                ChatHandler(player->GetSession()).SendSysMessage("Something went wrong when joining the queue.");

            player->CLOSE_GOSSIP_MENU();
            return true;
        }
        break;

        case 20: // Join Queue Arena (unrated)
        {
            if (Arena1v1CheckTalents(player) && JoinQueueArena(player, me, false) == false)
                ChatHandler(player->GetSession()).SendSysMessage("Something went wrong when joining the queue.");

            player->CLOSE_GOSSIP_MENU();
            return true;
        }
        break;

        case 3: // Leave Queue
        {
            WorldPacket Data;
            Data << (uint8)0x1 << (uint8)0x0 << (uint32)BATTLEGROUND_AA << (uint16)0x0 << (uint8)0x0;
            player->GetSession()->HandleBattleFieldPortOpcode(Data);
            player->CLOSE_GOSSIP_MENU();
            return true;
        }
        break;

        case 4: // get statistics
        {
            ArenaTeam* at = sArenaTeamMgr->GetArenaTeamById(player->GetArenaTeamId(ArenaTeam::GetSlotByType(ARENA_TEAM_5v5)));
            if (at)
            {
                std::stringstream s;
                s << "Rating: " << at->GetStats().Rating;
                s << "\nRank: " << at->GetStats().Rank;
                s << "\nSeason Games: " << at->GetStats().SeasonGames;
                s << "\nSeason Wins: " << at->GetStats().SeasonWins;
                s << "\nWeek Games: " << at->GetStats().WeekGames;
                s << "\nWeek Wins: " << at->GetStats().WeekWins;

                ChatHandler(player->GetSession()).PSendSysMessage(s.str().c_str());
            }
        }
        break;


        case 5: // Disband arenateam
        {
            WorldPacket Data;
            Data << (uint32)player->GetArenaTeamId(ArenaTeam::GetSlotByType(ARENA_TEAM_5v5));
            player->GetSession()->HandleArenaTeamLeaveOpcode(Data);
            ChatHandler(player->GetSession()).SendSysMessage("Arenateam deleted!");
            player->CLOSE_GOSSIP_MENU();
            return true;
        }
        break;
        /*
        case 8: // Script Info
        {
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "PWC", GOSSIP_SENDER_MAIN, uiAction);
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "PWC Scriptinfo", GOSSIP_SENDER_MAIN, uiAction);
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "PWC 1.0", GOSSIP_SENDER_MAIN, uiAction);
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "<-- Back", GOSSIP_SENDER_MAIN, 7);
        SendGossipMenuFor(player, 68, me->GetGUID());
        return true;
        }
        break;
        */
        }

        OnGossipHello(player, me);
        return true;
    }    // Return false, if player have invested more than 35 talentpoints in a forbidden talenttree.
};

void AddSC_npc_1v1arena()
{
    new arena1v1announce();
    new arena1v1_worldscript();
    new npc_1v1arena();
}
