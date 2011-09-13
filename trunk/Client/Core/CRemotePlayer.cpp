//============== IV: Multiplayer - http://code.iv-multiplayer.com ==============
//
// File: CRemotePlayer.cpp
// Project: Client.Core
// Author(s): jenksta
//            TrojaA
//            mabako
// License: See LICENSE in root directory
//
//==============================================================================

#include "CRemotePlayer.h"
#include "CChatWindow.h"
#include "CVehicleManager.h"
#include "CPlayerManager.h"
#include "Scripting.h"
#include "CLocalPlayer.h"

extern CChatWindow * g_pChatWindow;
extern CVehicleManager * g_pVehicleManager;
extern CLocalPlayer * g_pLocalPlayer;

CRemotePlayer::CRemotePlayer()
{
	m_vehicleId = INVALID_ENTITY_ID;
	m_uiBlipId = NULL;
	m_stateType = STATE_TYPE_DISCONNECT;
	m_bPassenger = false;
}

CRemotePlayer::~CRemotePlayer()
{
	Destroy();
}

bool CRemotePlayer::Spawn(CVector3 vecSpawnPos, float fSpawnHeading, bool bDontRecreate)
{
	if(!bDontRecreate)
	{
		if(IsSpawned())
			return false;

		if(!Create())
			return false;
	}

	//CLogFile::Printf("CIVPlayerPed + 0x218 = %d", *(BYTE *)(m_pPlayer->GetPlayerPed() + 0x218));
	Teleport(&vecSpawnPos);
	SetCurrentHeading(fSpawnHeading);
	Init();
	return true;
}

void CRemotePlayer::Destroy()
{
	//CNetworkPlayer::Destroy();

	// remove the blip
	if(m_uiBlipId != NULL)
	{
		Scripting::RemoveBlip(m_uiBlipId);
		m_uiBlipId = NULL;
	}
}

void CRemotePlayer::Kill()
{
	CNetworkPlayer::Kill();

	if(IsSpawned())
		m_stateType = STATE_TYPE_DEATH;
}

void CRemotePlayer::Init()
{
	if(IsSpawned())
	{
		Scripting::SetDontActivateRagdollFromPlayerImpact(GetPedHandle(), true);
		Scripting::AddBlipForChar(GetPedHandle(), &m_uiBlipId);
		Scripting::ChangeBlipSprite(m_uiBlipId, Scripting::BLIP_OBJECTIVE);
		Scripting::ChangeBlipScale(m_uiBlipId, 0.5);
		Scripting::ChangeBlipNameFromAscii(m_uiBlipId, GetName());
		//Nametags[GetPlayerId()].SetPed(GetPlayerPed()->GetPed());
		SetColor(GetColor());
		SetName(GetName());
		ToggleRagdoll(false);
		Scripting::SetPedDiesWhenInjured(GetPedHandle(), false);
		Scripting::SetCharInvincible(GetPedHandle(), true);
		// These two will be useful for a setPlayerUseModelAnims native
		//Scripting::SetAnimGroupForChar(m_pedIndex, "move_player");
		//Scripting::SetCharGestureGroup(m_pedIndex, "GESTURES@MALE");
		Scripting::BlockCharHeadIk(GetPedHandle(), true);
		m_stateType = STATE_TYPE_SPAWN;
	}
}

void CRemotePlayer::StoreOnFootSync(OnFootSyncData * syncPacket)
{
	// If we are in a vehicle remove ourselves from it
	if(IsInVehicle())
		RemoveFromVehicle();

	// Set their pad state
	SetNetPadState(&syncPacket->padState);

	// Set their position
	SetTargetPosition(syncPacket->vecPos, TICK_RATE);

	// Set their heading
	SetCurrentHeading(syncPacket->fHeading);

	// Set their move speed
	SetMoveSpeed(&syncPacket->vecMoveSpeed);

	// Set their ducking state
	SetDucking(syncPacket->bDuckState);

	// Set their health
	SetHealth(syncPacket->uHealthArmour >> 16);

	// Set their armour
	SetArmour((syncPacket->uHealthArmour << 16) >> 16);

	unsigned int uWeapon = (syncPacket->uWeaponInfo >> 20);
	unsigned int uAmmo = ((syncPacket->uWeaponInfo << 12) >> 12);

	if(GetCurrentWeapon() != uWeapon)
	{
		// Set their current weapon
		GiveWeapon(uWeapon, uAmmo);
		// TODO: Use swap weapon task instead?
	}
	else
	{
		// Set their current ammo
		SetAmmo(uWeapon, uAmmo);
	}

	m_stateType = STATE_TYPE_ONFOOT;
}

void CRemotePlayer::StoreAimSync(AimSyncData * syncPacket)
{
	// Set their aim sync data
	SetAimSyncData(syncPacket);
}

void CRemotePlayer::StoreInVehicleSync(EntityId vehicleId, InVehicleSyncData * syncPacket)
{
	// Get the vehicle
	CNetworkVehicle * pVehicle = g_pVehicleManager->Get(vehicleId);

	// Does the vehicle exist and is it spawned?
	if(pVehicle && pVehicle->IsStreamedIn())
	{
		// Store the vehicle id
		m_vehicleId = vehicleId;

		// If they have just warped into the vehicle and the local player
		// is driving it eject the local player
		if(m_stateType != STATE_TYPE_INVEHICLE && pVehicle->GetDriver() == g_pLocalPlayer)
		{
			g_pLocalPlayer->RemoveFromVehicle();
			g_pChatWindow->AddInfoMessage("Car Jacked");
		}

		// If they are not in the vehicle put them in it
		if(GetVehicle() != pVehicle)
		{
			if(g_pLocalPlayer->GetVehicle() == pVehicle && !g_pLocalPlayer->IsAPassenger())
				g_pLocalPlayer->RemoveFromVehicle();

			PutInVehicle(pVehicle, 0);
		}

		// Set their pad state
		SetNetPadState(&syncPacket->padState);

		// Set their vehicles target position
		pVehicle->SetTargetPosition(syncPacket->vecPos, TICK_RATE);

		// Set their vehicles target rotation
		pVehicle->SetTargetRotation(syncPacket->vecRotation, TICK_RATE);

		// Set their vehicles health
		// TODO: This should only be sent when it changes
		pVehicle->SetHealth(syncPacket->uiHealth);

		// Set their vehicles color
		// TODO: This should only be sent when it changes
		pVehicle->SetColors(syncPacket->byteColors[0], syncPacket->byteColors[1], syncPacket->byteColors[2], syncPacket->byteColors[3]);

		// Set their vehicles siren state
		// TODO: This should only be sent when it changes
		pVehicle->SetSirenState(syncPacket->bSirenState);

		// Set their vehicles turn speed
		pVehicle->SetTurnSpeed(&syncPacket->vecTurnSpeed);

		// Set their vehicles move speed
		pVehicle->SetMoveSpeed(&syncPacket->vecMoveSpeed);

		// Set their vehicles dirt level
		// TODO: This should only be sent when it changes
		pVehicle->SetDirtLevel(syncPacket->fDirtLevel);

		// Set their health
		SetHealth(syncPacket->uPlayerHealthArmour >> 16);

		// Set their armour
		SetArmour((syncPacket->uPlayerHealthArmour << 16) >> 16);

		// NOTE: Setting weapon and ammo does not work
		// in vehicles, fix it

		// Set their current weapon
		unsigned int uPlayerWeapon = (syncPacket->uPlayerWeaponInfo >> 20);
		SetCurrentWeapon(uPlayerWeapon);

		// Set their current ammo
		SetAmmo(uPlayerWeapon, ((syncPacket->uPlayerWeaponInfo << 12) >> 12));

	//Is the vehicle streamed out?
	}else if(pVehicle && !(pVehicle->IsStreamedIn())){
		//set the vehicle position
		pVehicle->SetPosition(syncPacket->vecPos, false, true);

		//set the player position
		SetPosition(&(syncPacket->vecPos), true);
	}

	m_stateType = STATE_TYPE_INVEHICLE;
}

void CRemotePlayer::StorePassengerSync(EntityId vehicleId, PassengerSyncData * syncPacket)
{
	// Get the vehicle
	CNetworkVehicle * pVehicle = g_pVehicleManager->Get(vehicleId);

	// Does the vehicle exist and is it spawned?
	if(pVehicle && pVehicle->IsStreamedIn())
	{
		// Store the vehicle id
		m_vehicleId = vehicleId;

		// If they are not in the vehicle put them in it
		if(GetVehicle() != pVehicle)
			PutInVehicle(pVehicle, 0);

		// Set their pad state
		SetNetPadState(&syncPacket->padState);

		// Set their health
		SetHealth(syncPacket->uPlayerHealthArmour >> 16);

		// Set their armour
		SetArmour((syncPacket->uPlayerHealthArmour << 16) >> 16);

		// NOTE: Setting weapon and ammo does not work
		// in vehicles, fix it

		// Set their current weapon
		unsigned int uPlayerWeapon = (syncPacket->uPlayerWeaponInfo >> 20);
		SetCurrentWeapon(uPlayerWeapon);

		// Set their current ammo
		SetAmmo(uPlayerWeapon, ((syncPacket->uPlayerWeaponInfo << 12) >> 12));
	}

	m_stateType = STATE_TYPE_PASSENGER;
}

void CRemotePlayer::StoreSmallSync(SmallSyncData * syncPacket)
{
	// Set their pad state
	SetNetPadState(&syncPacket->padState);

	// Set their ducking state
	SetDucking(syncPacket->bDuckState);

	unsigned int uWeapon = (syncPacket->uWeaponInfo >> 20);
	unsigned int uAmmo = ((syncPacket->uWeaponInfo << 12) >> 12);

	if(GetCurrentWeapon() != uWeapon)
	{
		// Set their current weapon
		GiveWeapon(uWeapon, uAmmo);
		// TODO: Use swap weapon task instead?
	}
	else
	{
		// Set their current ammo
		SetAmmo(uWeapon, uAmmo);
	}
}

void CRemotePlayer::SetColor(unsigned int uiColor)
{
	CNetworkPlayer::SetColor(uiColor);

	if(m_uiBlipId != NULL)
		Scripting::ChangeBlipColour(m_uiBlipId, uiColor);

}