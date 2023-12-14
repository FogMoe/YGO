#include "single_mode.h"
#include "duelclient.h"
#include "game.h"
#include "../ocgcore/common.h"
#include "../ocgcore/mtrandom.h"

namespace ygo {

intptr_t SingleMode::pduel = 0;
bool SingleMode::is_closing = false;
bool SingleMode::is_continuing = false;
Replay SingleMode::last_replay;

bool SingleMode::StartPlay() {
	std::thread(SinglePlayThread).detach();
	return true;
}
void SingleMode::StopPlay(bool is_exiting) {
	is_closing = is_exiting;
	is_continuing = false;
	mainGame->actionSignal.Set();
	mainGame->singleSignal.Set();
}
void SingleMode::SetResponse(unsigned char* resp, unsigned int len) {
	if(!pduel)
		return;
	last_replay.WriteInt8(len);
	last_replay.WriteData(resp, len);
	set_responseb(pduel, resp);
}
int SingleMode::SinglePlayThread() {
	const int start_lp = 8000;
	const int start_hand = 5;
	const int draw_count = 1;
	int opt = 0;
	std::random_device rd;
	unsigned int seed = rd();
	mt19937 rnd((uint_fast32_t)seed);
	set_script_reader((script_reader)DataManager::ScriptReaderEx);
	set_card_reader((card_reader)DataManager::CardReader);
	set_message_handler((message_handler)MessageHandler);
	pduel = create_duel(rnd.rand());
	set_player_info(pduel, 0, start_lp, start_hand, draw_count);
	set_player_info(pduel, 1, start_lp, start_hand, draw_count);
	mainGame->dInfo.lp[0] = start_lp;
	mainGame->dInfo.lp[1] = start_lp;
	mainGame->dInfo.start_lp = start_lp;
	myswprintf(mainGame->dInfo.strLP[0], L"%d", mainGame->dInfo.lp[0]);
	myswprintf(mainGame->dInfo.strLP[1], L"%d", mainGame->dInfo.lp[1]);
	BufferIO::CopyWStr(mainGame->ebNickName->getText(), mainGame->dInfo.hostname, 20);
	mainGame->dInfo.clientname[0] = 0;
	mainGame->dInfo.player_type = 0;
	mainGame->dInfo.turn = 0;
	if(mainGame->chkSinglePlayReturnDeckTop->isChecked())
		opt |= DUEL_RETURN_DECK_TOP;
	char filename[256];
	size_t slen = 0;
	if(open_file) {
		open_file = false;
		slen = BufferIO::EncodeUTF8(open_file_name, filename);
		if(!preload_script(pduel, filename, 0)) {
			wchar_t fname[256];
			myswprintf(fname, L"./single/%ls", open_file_name);
			slen = BufferIO::EncodeUTF8(fname, filename);
			if(!preload_script(pduel, filename, 0))
				slen = 0;
		}
	} else {
		const wchar_t* name = mainGame->lstSinglePlayList->getListItem(mainGame->lstSinglePlayList->getSelected());
		wchar_t fname[256];
		myswprintf(fname, L"./single/%ls", name);
		slen = BufferIO::EncodeUTF8(fname, filename);
		if(!preload_script(pduel, filename, 0))
			slen = 0;
	}
	if(slen == 0) {
		end_duel(pduel);
		return 0;
	}
	ReplayHeader rh;
	rh.id = 0x31707279;
	rh.version = PRO_VERSION;
	rh.flag = REPLAY_UNIFORM | REPLAY_SINGLE_MODE;
	rh.seed = seed;
	rh.start_time = (unsigned int)time(nullptr);
	mainGame->gMutex.lock();
	mainGame->HideElement(mainGame->wSinglePlay);
	mainGame->ClearCardInfo();
	mainGame->wCardImg->setVisible(true);
	mainGame->wInfos->setVisible(true);
	mainGame->btnLeaveGame->setVisible(true);
	mainGame->btnLeaveGame->setText(dataManager.GetSysString(1210));
	mainGame->wPhase->setVisible(true);
	mainGame->dField.Clear();
	mainGame->dInfo.isFirst = true;
	mainGame->dInfo.isStarted = true;
	mainGame->dInfo.isFinished = false;
	mainGame->dInfo.isSingleMode = true;
	mainGame->device->setEventReceiver(&mainGame->dField);
	mainGame->gMutex.unlock();
	std::vector<char> engineBuffer;
	engineBuffer.resize(SIZE_MESSAGE_BUFFER);
	is_closing = false;
	is_continuing = true;
	int len = get_message(pduel, (byte*)engineBuffer.data());
	if (len > 0)
		is_continuing = SinglePlayAnalyze(engineBuffer.data(), len);
	last_replay.BeginRecord();
	last_replay.WriteHeader(rh);
	unsigned short buffer[20];
	BufferIO::CopyWStr(mainGame->dInfo.hostname, buffer, 20);
	last_replay.WriteData(buffer, 40, false);
	BufferIO::CopyWStr(mainGame->dInfo.clientname, buffer, 20);
	last_replay.WriteData(buffer, 40, false);
	last_replay.WriteInt32(start_lp, false);
	last_replay.WriteInt32(start_hand, false);
	last_replay.WriteInt32(draw_count, false);
	last_replay.WriteInt32(opt, false);
	last_replay.WriteInt16(slen, false);
	last_replay.WriteData(filename, slen, false);
	last_replay.Flush();
	start_duel(pduel, opt);
	while (is_continuing) {
		unsigned int result = process(pduel);
		len = result & PROCESSOR_BUFFER_LEN;
		if (len > 0) {
			if (len > (int)engineBuffer.size())
				engineBuffer.resize(len);
			get_message(pduel, (byte*)engineBuffer.data());
			is_continuing = SinglePlayAnalyze(engineBuffer.data(), len);
		}
	}
	last_replay.EndRecord();
	mainGame->gMutex.lock();
	time_t nowtime = time(NULL);
	tm* localedtime = localtime(&nowtime);
	wchar_t timetext[40];
	wcsftime(timetext, 40, L"%Y-%m-%d %H-%M-%S", localedtime);
	mainGame->ebRSName->setText(timetext);
	if(!mainGame->chkAutoSaveReplay->isChecked()) {
		mainGame->wReplaySave->setText(dataManager.GetSysString(1340));
		mainGame->PopupElement(mainGame->wReplaySave);
		mainGame->gMutex.unlock();
		mainGame->replaySignal.Reset();
		mainGame->replaySignal.Wait();
	} else {
		mainGame->actionParam = 1;
		wchar_t msgbuf[256];
		myswprintf(msgbuf, dataManager.GetSysString(1367), timetext);
		mainGame->SetStaticText(mainGame->stACMessage, 310, mainGame->guiFont, msgbuf);
		mainGame->PopupElement(mainGame->wACMessage, 20);
		mainGame->gMutex.unlock();
		mainGame->WaitFrameSignal(30);
	}
	if(mainGame->actionParam)
		last_replay.SaveReplay(mainGame->ebRSName->getText());
	end_duel(pduel);
	if(!is_closing) {
		mainGame->gMutex.lock();
		mainGame->dInfo.isStarted = false;
		mainGame->dInfo.isFinished = true;
		mainGame->dInfo.isSingleMode = false;
		mainGame->gMutex.unlock();
		mainGame->closeDoneSignal.Reset();
		mainGame->closeSignal.Set();
		mainGame->closeDoneSignal.Wait();
		mainGame->gMutex.lock();
		mainGame->ShowElement(mainGame->wSinglePlay);
		mainGame->stTip->setVisible(false);
		mainGame->device->setEventReceiver(&mainGame->menuHandler);
		mainGame->gMutex.unlock();
		if(exit_on_return)
			mainGame->device->closeDevice();
	}
	return 0;
}
bool SingleMode::SinglePlayAnalyze(char* msg, unsigned int len) {
	char* offset, *pbuf = msg;
	int player, count;
	while (pbuf - msg < (int)len) {
		if(is_closing || !is_continuing)
			return false;
		offset = pbuf;
		mainGame->dInfo.curMsg = BufferIO::ReadUInt8(pbuf);
		switch (mainGame->dInfo.curMsg) {
		case MSG_RETRY: {
			if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
				mainGame->singleSignal.Reset();
				mainGame->singleSignal.Wait();
			}
			break;
		}
		case MSG_HINT: {
			/*int type = */BufferIO::ReadInt8(pbuf);
			int player = BufferIO::ReadInt8(pbuf);
			/*int data = */BufferIO::ReadInt32(pbuf);
			if(player == 0)
				DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_WIN: {
			pbuf += 2;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			return false;
		}
		case MSG_SELECT_BATTLECMD: {
			player = BufferIO::ReadInt8(pbuf);
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 11;
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 8 + 2;
			SinglePlayRefresh();
			if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
				mainGame->singleSignal.Reset();
				mainGame->singleSignal.Wait();
			}
			break;
		}
		case MSG_SELECT_IDLECMD: {
			player = BufferIO::ReadInt8(pbuf);
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 7;
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 7;
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 7;
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 7;
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 7;
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 11 + 3;
			SinglePlayRefresh();
			if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
				mainGame->singleSignal.Reset();
				mainGame->singleSignal.Wait();
			}
			break;
		}
		case MSG_SELECT_EFFECTYN: {
			player = BufferIO::ReadInt8(pbuf);
			pbuf += 12;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
				mainGame->singleSignal.Reset();
				mainGame->singleSignal.Wait();
			}
			break;
		}
		case MSG_SELECT_YESNO: {
			player = BufferIO::ReadInt8(pbuf);
			pbuf += 4;
			if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
				mainGame->singleSignal.Reset();
				mainGame->singleSignal.Wait();
			}
			break;
		}
		case MSG_SELECT_OPTION: {
			player = BufferIO::ReadInt8(pbuf);
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 4;
			if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
				mainGame->singleSignal.Reset();
				mainGame->singleSignal.Wait();
			}
			break;
		}
		case MSG_SELECT_CARD:
		case MSG_SELECT_TRIBUTE: {
			player = BufferIO::ReadInt8(pbuf);
			pbuf += 3;
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 8;
			if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
				mainGame->singleSignal.Reset();
				mainGame->singleSignal.Wait();
			}
			break;
		}
		case MSG_SELECT_UNSELECT_CARD: {
			player = BufferIO::ReadInt8(pbuf);
			pbuf += 4;
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 8;
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 8;
			if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
				mainGame->singleSignal.Reset();
				mainGame->singleSignal.Wait();
			}
			break;
		}
		case MSG_SELECT_CHAIN: {
			player = BufferIO::ReadInt8(pbuf);
			count = BufferIO::ReadInt8(pbuf);
			pbuf += 10 + count * 13;
			if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
				mainGame->singleSignal.Reset();
				mainGame->singleSignal.Wait();
			}
			break;
		}
		case MSG_SELECT_PLACE:
		case MSG_SELECT_DISFIELD: {
			player = BufferIO::ReadInt8(pbuf);
			pbuf += 5;
			if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
				mainGame->singleSignal.Reset();
				mainGame->singleSignal.Wait();
			}
			break;
		}
		case MSG_SELECT_POSITION: {
			player = BufferIO::ReadInt8(pbuf);
			pbuf += 5;
			if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
				mainGame->singleSignal.Reset();
				mainGame->singleSignal.Wait();
			}
			break;
		}
		case MSG_SELECT_COUNTER: {
			player = BufferIO::ReadInt8(pbuf);
			pbuf += 4;
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 9;
			if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
				mainGame->singleSignal.Reset();
				mainGame->singleSignal.Wait();
			}
			break;
		}
		case MSG_SELECT_SUM: {
			pbuf++;
			player = BufferIO::ReadInt8(pbuf);
			pbuf += 6;
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 11;
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 11;
			if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
				mainGame->singleSignal.Reset();
				mainGame->singleSignal.Wait();
			}
			break;
		}
		case MSG_SORT_CARD: {
			player = BufferIO::ReadInt8(pbuf);
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 7;
			if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
				mainGame->singleSignal.Reset();
				mainGame->singleSignal.Wait();
			}
			break;
		}
		case MSG_CONFIRM_DECKTOP: {
			player = BufferIO::ReadInt8(pbuf);
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 7;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_CONFIRM_EXTRATOP: {
			player = BufferIO::ReadInt8(pbuf);
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 7;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_CONFIRM_CARDS: {
			player = BufferIO::ReadInt8(pbuf);
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 7;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_SHUFFLE_DECK: {
			player = BufferIO::ReadInt8(pbuf);
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefreshDeck(player);
			break;
		}
		case MSG_SHUFFLE_HAND: {
			/*int oplayer = */BufferIO::ReadInt8(pbuf);
			int count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 4;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_SHUFFLE_EXTRA: {
			player = BufferIO::ReadInt8(pbuf);
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 4;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_REFRESH_DECK: {
			pbuf++;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_SWAP_GRAVE_DECK: {
			player = BufferIO::ReadInt8(pbuf);
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefreshGrave(player);
			break;
		}
		case MSG_REVERSE_DECK: {
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefreshDeck(0);
			SinglePlayRefreshDeck(1);
			break;
		}
		case MSG_DECK_TOP: {
			pbuf += 6;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_SHUFFLE_SET_CARD: {
			pbuf++;
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 8;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_NEW_TURN: {
			player = BufferIO::ReadInt8(pbuf);
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_NEW_PHASE: {
			pbuf += 2;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefresh();
			break;
		}
		case MSG_MOVE: {
			int pc = pbuf[4];
			int pl = pbuf[5];
			/*int ps = pbuf[6];*/
			/*int pp = pbuf[7];*/
			int cc = pbuf[8];
			int cl = pbuf[9];
			int cs = pbuf[10];
			/*int cp = pbuf[11];*/
			pbuf += 16;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			if(cl && !(cl & LOCATION_OVERLAY) && (pl != cl || pc != cc))
				SinglePlayRefreshSingle(cc, cl, cs);
			break;
		}
		case MSG_POS_CHANGE: {
			pbuf += 9;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_SET: {
			pbuf += 8;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_SWAP: {
			pbuf += 16;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_FIELD_DISABLED: {
			pbuf += 4;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_SUMMONING: {
			pbuf += 8;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_SUMMONED: {
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefresh();
			break;
		}
		case MSG_SPSUMMONING: {
			pbuf += 8;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_SPSUMMONED: {
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefresh();
			break;
		}
		case MSG_FLIPSUMMONING: {
			pbuf += 8;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_FLIPSUMMONED: {
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefresh();
			break;
		}
		case MSG_CHAINING: {
			pbuf += 16;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_CHAINED: {
			pbuf++;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefresh();
			break;
		}
		case MSG_CHAIN_SOLVING: {
			pbuf++;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_CHAIN_SOLVED: {
			pbuf++;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefresh();
			break;
		}
		case MSG_CHAIN_END: {
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefresh();
			SinglePlayRefreshDeck(0);
			SinglePlayRefreshDeck(1);
			break;
		}
		case MSG_CHAIN_NEGATED: {
			pbuf++;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_CHAIN_DISABLED: {
			pbuf++;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_CARD_SELECTED:
		case MSG_RANDOM_SELECTED: {
			player = BufferIO::ReadInt8(pbuf);
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 4;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_BECOME_TARGET: {
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 4;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_DRAW: {
			player = BufferIO::ReadInt8(pbuf);
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 4;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_DAMAGE: {
			pbuf += 5;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_RECOVER: {
			pbuf += 5;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_EQUIP: {
			pbuf += 8;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_LPUPDATE: {
			pbuf += 5;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_UNEQUIP: {
			pbuf += 4;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_CARD_TARGET: {
			pbuf += 8;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_CANCEL_TARGET: {
			pbuf += 8;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_PAY_LPCOST: {
			pbuf += 5;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_ADD_COUNTER: {
			pbuf += 7;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_REMOVE_COUNTER: {
			pbuf += 7;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_ATTACK: {
			pbuf += 8;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_BATTLE: {
			pbuf += 26;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_ATTACK_DISABLED: {
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_DAMAGE_STEP_START: {
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefresh();
			break;
		}
		case MSG_DAMAGE_STEP_END: {
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefresh();
			break;
		}
		case MSG_MISSED_EFFECT: {
			pbuf += 8;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_TOSS_COIN: {
			player = BufferIO::ReadInt8(pbuf);
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_TOSS_DICE: {
			player = BufferIO::ReadInt8(pbuf);
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_ROCK_PAPER_SCISSORS: {
			player = BufferIO::ReadInt8(pbuf);
			if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
				mainGame->singleSignal.Reset();
				mainGame->singleSignal.Wait();
			}
			break;
		}
		case MSG_HAND_RES: {
			pbuf += 1;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_ANNOUNCE_RACE: {
			player = BufferIO::ReadInt8(pbuf);
			pbuf += 5;
			if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
				mainGame->singleSignal.Reset();
				mainGame->singleSignal.Wait();
			}
			break;
		}
		case MSG_ANNOUNCE_ATTRIB: {
			player = BufferIO::ReadInt8(pbuf);
			pbuf += 5;
			if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
				mainGame->singleSignal.Reset();
				mainGame->singleSignal.Wait();
			}
			break;
		}
		case MSG_ANNOUNCE_CARD:
		case MSG_ANNOUNCE_NUMBER: {
			player = BufferIO::ReadInt8(pbuf);
			count = BufferIO::ReadUInt8(pbuf);
			pbuf += 4 * count;
			if(!DuelClient::ClientAnalyze(offset, pbuf - offset)) {
				mainGame->singleSignal.Reset();
				mainGame->singleSignal.Wait();
			}
			break;
		}
		case MSG_CARD_HINT: {
			pbuf += 9;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_PLAYER_HINT: {
			pbuf += 6;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			break;
		}
		case MSG_TAG_SWAP: {
			player = pbuf[0];
			pbuf += pbuf[2] * 4 + pbuf[4] * 4 + 9;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayRefreshDeck(player);
			SinglePlayRefreshExtra(player);
			break;
		}
		case MSG_MATCH_KILL: {
			pbuf += 4;
			break;
		}
		case MSG_RELOAD_FIELD: {
			pbuf++;
			for(int p = 0; p < 2; ++p) {
				pbuf += 4;
				for(int seq = 0; seq < 7; ++seq) {
					int val = BufferIO::ReadInt8(pbuf);
					if(val)
						pbuf += 2;
				}
				for(int seq = 0; seq < 8; ++seq) {
					int val = BufferIO::ReadInt8(pbuf);
					if(val)
						pbuf++;
				}
				pbuf += 6;
			}
			count = BufferIO::ReadInt8(pbuf);
			pbuf += count * 15;
			DuelClient::ClientAnalyze(offset, pbuf - offset);
			SinglePlayReload();
			mainGame->gMutex.lock();
			mainGame->dField.RefreshAllCards();
			mainGame->gMutex.unlock();
			break;
		}
		case MSG_AI_NAME: {
			char namebuf[128];
			wchar_t wname[128];
			int len = BufferIO::ReadInt16(pbuf);
			char* begin = pbuf;
			pbuf += len + 1;
			memcpy(namebuf, begin, len + 1);
			BufferIO::DecodeUTF8(namebuf, wname);
			BufferIO::CopyWStr(wname, mainGame->dInfo.clientname, 20);
			break;
		}
		case MSG_SHOW_HINT: {
			char msgbuf[1024];
			wchar_t msg[1024];
			int len = BufferIO::ReadInt16(pbuf);
			char* begin = pbuf;
			pbuf += len + 1;
			memcpy(msgbuf, begin, len + 1);
			BufferIO::DecodeUTF8(msgbuf, msg);
			mainGame->gMutex.lock();
			mainGame->SetStaticText(mainGame->stMessage, 310, mainGame->guiFont, msg);
			mainGame->PopupElement(mainGame->wMessage);
			mainGame->gMutex.unlock();
			mainGame->actionSignal.Reset();
			mainGame->actionSignal.Wait();
			break;
		}
		}
	}
	return is_continuing;
}
void SingleMode::ReloadLocation(int player, int location, int flag, std::vector<unsigned char>& queryBuffer) {
	query_field_card(pduel, player, location, flag, queryBuffer.data(), 0);
	mainGame->dField.UpdateFieldCard(mainGame->LocalPlayer(player), location, (char*)queryBuffer.data());
}
void SingleMode::RefreshLocation(int player, int location, int flag) {
	std::vector<unsigned char> queryBuffer;
	queryBuffer.reserve(SIZE_QUERY_BUFFER);
	ReloadLocation(player, location, flag, queryBuffer);
}
void SingleMode::SinglePlayRefresh(int flag) {
	std::vector<unsigned char> queryBuffer;
	queryBuffer.reserve(SIZE_QUERY_BUFFER);
	ReloadLocation(0, LOCATION_MZONE, flag, queryBuffer);
	ReloadLocation(1, LOCATION_MZONE, flag, queryBuffer);
	ReloadLocation(0, LOCATION_SZONE, flag, queryBuffer);
	ReloadLocation(1, LOCATION_SZONE, flag, queryBuffer);
	ReloadLocation(0, LOCATION_HAND, flag, queryBuffer);
	ReloadLocation(1, LOCATION_HAND, flag, queryBuffer);
}
void SingleMode::SinglePlayRefreshHand(int player, int flag) {
	RefreshLocation(player, LOCATION_HAND, flag);
}
void SingleMode::SinglePlayRefreshGrave(int player, int flag) {
	RefreshLocation(player, LOCATION_GRAVE, flag);
}
void SingleMode::SinglePlayRefreshDeck(int player, int flag) {
	RefreshLocation(player, LOCATION_DECK, flag);
}
void SingleMode::SinglePlayRefreshExtra(int player, int flag) {
	RefreshLocation(player, LOCATION_EXTRA, flag);
}
void SingleMode::SinglePlayRefreshSingle(int player, int location, int sequence, int flag) {
	unsigned char queryBuffer[0x2000];
	/*int len = */query_card(pduel, player, location, sequence, flag, queryBuffer, 0);
	mainGame->dField.UpdateCard(mainGame->LocalPlayer(player), location, sequence, (char*)queryBuffer);
}
void SingleMode::SinglePlayReload() {
	std::vector<byte> queryBuffer;
	queryBuffer.reserve(SIZE_QUERY_BUFFER);
	unsigned int flag = 0xffdfff;
	ReloadLocation(0, LOCATION_MZONE, flag, queryBuffer);
	ReloadLocation(1, LOCATION_MZONE, flag, queryBuffer);
	ReloadLocation(0, LOCATION_SZONE, flag, queryBuffer);
	ReloadLocation(1, LOCATION_SZONE, flag, queryBuffer);
	ReloadLocation(0, LOCATION_HAND, flag, queryBuffer);
	ReloadLocation(1, LOCATION_HAND, flag, queryBuffer);

	ReloadLocation(0, LOCATION_DECK, flag, queryBuffer);
	ReloadLocation(1, LOCATION_DECK, flag, queryBuffer);
	ReloadLocation(0, LOCATION_EXTRA, flag, queryBuffer);
	ReloadLocation(1, LOCATION_EXTRA, flag, queryBuffer);
	ReloadLocation(0, LOCATION_GRAVE, flag, queryBuffer);
	ReloadLocation(1, LOCATION_GRAVE, flag, queryBuffer);
	ReloadLocation(0, LOCATION_REMOVED, flag, queryBuffer);
	ReloadLocation(1, LOCATION_REMOVED, flag, queryBuffer);
}
int SingleMode::MessageHandler(intptr_t fduel, int type) {
	if(!enable_log)
		return 0;
	char msgbuf[1024];
	get_log_message(fduel, (byte*)msgbuf);
	mainGame->AddDebugMsg(msgbuf);
	return 0;
}

}
