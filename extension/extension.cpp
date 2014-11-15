/**
 * vim: set ts=4 :
 * =============================================================================
 * SendVar Proxy Manager
 * Copyright (C) 2011 Afronanny.  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */
#ifdef _WIN32
#undef GetProp
#endif
#include "extension.h"
#include "extensions/IWebternet.h"
#include "gamedataupdater.h"

IWebternet *webternet = NULL;
void TryUpdate();
bool g_bDoLateUpdate = false;
/**
 * @file extension.cpp
 * @brief Implement extension code here.
 */

SH_DECL_MANUALHOOK0_void(UpdateOnRemove, 0, 0, 0);
SH_DECL_HOOK1_void(IServerGameClients, ClientDisconnect, SH_NOATTRIB, false, edict_t*);
SH_DECL_HOOK1_void(IServerGameDLL, GameFrame, SH_NOATTRIB, false, bool);

SendProxyManager g_SendProxyManager;		/**< Global singleton for extension's main interface */
SMEXT_LINK(&g_SendProxyManager);

CThreadFastMutex g_WorkMutex;

CUtlVector<SendPropHook> g_Hooks;
CUtlVector<PropChangeHook> g_ChangeHooks;

IServerGameEnts *gameents = NULL;
IServerGameClients *gameclients = NULL;
IGameConfig *g_pGameConf = NULL;

static cell_t Native_Hook(IPluginContext* pContext, const cell_t* params);
static cell_t Native_Unhook(IPluginContext* pContext, const cell_t* params);
static cell_t Native_IsHooked(IPluginContext* pContext, const cell_t* params);
static cell_t Native_HookArrayProp(IPluginContext* pContext, const cell_t* params);
static cell_t Native_UnhookArrayProp(IPluginContext* pContext, const cell_t* params);
static cell_t Native_HookPropChange(IPluginContext* pContext, const cell_t* params);
static cell_t Native_UnhookPropChange(IPluginContext* pContext, const cell_t* params);

const sp_nativeinfo_t g_MyNatives[] = {
	{"SendProxy_Hook", Native_Hook},
	{"SendProxy_HookArrayProp", Native_HookArrayProp},
	{"SendProxy_UnhookArrayProp", Native_UnhookArrayProp},
	{"SendProxy_Unhook", Native_Unhook},
	{"SendProxy_IsHooked", Native_IsHooked},
	{"SendProxy_HookPropChange", Native_HookPropChange},
	{"SendProxy_UnhookPropChange", Native_UnhookPropChange},
	{NULL,	NULL},
};

ConVar sendproxy_autoupdate( "sendproxy_autoupdate", "0", FCVAR_NONE, "Enable or disable automatic gamedata updating for the sendproxy extension." );


void Hook_UpdateOnRemove()
{
	CBaseEntity *pEnt = META_IFACEPTR(CBaseEntity);
	int idx = gamehelpers->EntityToBCompatRef(pEnt);
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].objectID == idx)
		{
			g_SendProxyManager.UnhookProxy(i);
		}
	}

	for (int i = 0; i < g_ChangeHooks.Count(); i++)
	{
		if (g_ChangeHooks[i].objectID == idx)
			g_ChangeHooks.Remove(i--);
	}
	SH_REMOVE_MANUALHOOK(UpdateOnRemove, pEnt, SH_STATIC(Hook_UpdateOnRemove), false);
	RETURN_META(MRES_IGNORED);
}

void Hook_ClientDisconnect(edict_t* pEnt)
{
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].objectID == engine->IndexOfEdict(pEnt))
			g_SendProxyManager.UnhookProxy(i);
	}

	for (int i = 0; i < g_ChangeHooks.Count(); i++)
	{
		if (g_ChangeHooks[i].objectID == engine->IndexOfEdict(pEnt))
			g_ChangeHooks.Remove(i--);
	}
	RETURN_META(MRES_IGNORED);
}

void Hook_GameFrame(bool simulating)
{
	if (simulating)
	{
		for (int i = 0; i < g_ChangeHooks.Count(); i++)
		{
			switch(g_ChangeHooks[i].PropType)
			{
			case Prop_Int:
				{
					edict_t* pEnt = engine->PEntityOfEntIndex(g_ChangeHooks[i].objectID);
					CBaseEntity* pEntity = gameents->EdictToBaseEntity(pEnt);
					int iCurrent = *(int*)((unsigned char*)pEntity + g_ChangeHooks[i].Offset);
					if (iCurrent != g_ChangeHooks[i].iLastValue)
						{
						g_ChangeHooks[i].pCallback->PushCell(g_ChangeHooks[i].objectID);
						g_ChangeHooks[i].pCallback->PushString(g_ChangeHooks[i].pVar->GetName());
						char oldValue[64];
						snprintf(oldValue, 64, "%d", g_ChangeHooks[i].iLastValue);
						char newValue[64];
						snprintf(newValue, 64, "%d", iCurrent);
						g_ChangeHooks[i].pCallback->PushString(oldValue);
						g_ChangeHooks[i].pCallback->PushString(newValue);
						g_ChangeHooks[i].pCallback->Execute(0);
						g_ChangeHooks[i].iLastValue = iCurrent;
					}
					break;
				}
			case Prop_Float:
				{
					edict_t* pEnt = engine->PEntityOfEntIndex(g_ChangeHooks[i].objectID);
					CBaseEntity* pEntity = gameents->EdictToBaseEntity(pEnt);
					float flCurrent = *(float*)((unsigned char*)pEntity + g_ChangeHooks[i].Offset);
					if (flCurrent != g_ChangeHooks[i].flLastValue)
					{
						g_ChangeHooks[i].pCallback->PushCell(g_ChangeHooks[i].objectID);
						g_ChangeHooks[i].pCallback->PushString(g_ChangeHooks[i].pVar->GetName());
						char oldValue[64];
						snprintf(oldValue, 64, "%f", g_ChangeHooks[i].flLastValue);
						char newValue[64];
						snprintf(newValue, 64, "%f", flCurrent);
						g_ChangeHooks[i].pCallback->PushString(oldValue);
						g_ChangeHooks[i].pCallback->PushString(newValue);
						g_ChangeHooks[i].pCallback->Execute(0);
						g_ChangeHooks[i].flLastValue = flCurrent;
					}
					break;
				}
			case Prop_String:
				{
					edict_t* pEnt = engine->PEntityOfEntIndex(g_ChangeHooks[i].objectID);
					CBaseEntity* pEntity = gameents->EdictToBaseEntity(pEnt);
					const char* szCurrent = (const char*)((unsigned char*)pEntity + g_ChangeHooks[i].Offset);
					if (strcmp(szCurrent, g_ChangeHooks[i].szLastValue.c_str()) != 0)
					{
						g_ChangeHooks[i].pCallback->PushCell(g_ChangeHooks[i].objectID);
						g_ChangeHooks[i].pCallback->PushString(g_ChangeHooks[i].pVar->GetName());
						g_ChangeHooks[i].pCallback->PushString(g_ChangeHooks[i].szLastValue.c_str());
						g_ChangeHooks[i].pCallback->PushString(szCurrent);
						g_ChangeHooks[i].pCallback->Execute(0);
						g_ChangeHooks[i].szLastValue.clear();
						g_ChangeHooks[i].szLastValue.append(szCurrent);
					}
					break;
				}
			default:
				{
					//earlier typechecks failed
				}
			}
		}
	}
	RETURN_META(MRES_IGNORED);
}
bool SendProxyManager::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
	SM_GET_LATE_IFACE(WEBTERNET, webternet);
	if (!webternet)
	{
		g_bDoLateUpdate = true;
		g_pSM->LogMessage(myself, "Could not get Webternet iface yet - skipping pre-load gamedata update check and trying later");
	} else {
		TryUpdate();
	}

	char conf_error[255];
	if (!gameconfs->LoadGameConfigFile("sendproxy", &g_pGameConf, conf_error, sizeof(conf_error)))
	{
		if (conf_error[0])
			snprintf(error, maxlength, "Could not read config file sendproxy.txt: %s", conf_error);
		return false;
	}

	int offset = 0;
	g_pGameConf->GetOffset("UpdateOnRemove", &offset);
	if (offset > 0)
	{
		SH_MANUALHOOK_RECONFIGURE(UpdateOnRemove, offset, 0, 0);
	} else {
		snprintf(error, maxlength, "Could not get offset for UpdateOnRemove");
		return false;
	}

	sharesys->RegisterLibrary(myself, "sendproxy");
	plsys->AddPluginsListener(&g_SendProxyManager);

	return true;
}

void TryUpdate()
{
	if ( !sendproxy_autoupdate.GetBool() )
		return; // auto update disabled

	IWebTransfer *transfer = webternet->CreateSession();
	Downloader downloader = Downloader();
	if (!transfer->Download("http://gamedata.afronanny.org/sendproxy.txt", &downloader, NULL))
	{
		g_pSM->LogMessage(myself, "Error downloading sendproxy.txt: %s", transfer->LastErrorMessage());
		delete transfer;
		return;
	}
	std::string str(downloader.buffer);
	size_t found = str.find("404");
	if (found != std::string::npos)
	{
		g_pSM->LogMessage(myself, "Gamedata not found on server! 404");
		delete transfer;
		return;
	}
	char path[PLATFORM_MAX_PATH];
	g_pSM->BuildPath(Path_SM, path, sizeof(path), "gamedata/sendproxy.txt");
	FILE *fp = fopen(path, "w");
	if (!fp)
	{
		g_pSM->LogMessage(myself, "Could not open sendproxy.txt for updating");
		delete transfer;
		return;
	}
	fwrite(downloader.buffer, 1, downloader.pos, fp);
	fclose(fp);
	g_bDoLateUpdate = false;
	delete transfer;
}

void SendProxyManager::SDK_OnAllLoaded()
{
	sharesys->AddNatives(myself, g_MyNatives);
	if (g_bDoLateUpdate && !webternet)
	{
		SM_GET_LATE_IFACE(WEBTERNET, webternet);
		if (!webternet)
		{
			g_pSM->LogMessage(myself, "Still can't get Webternet interface, skipping gamedata download");
			return;
		}
		TryUpdate();
	}
}

void SendProxyManager::SDK_OnUnload()
{
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		g_Hooks[i].pVar->SetProxyFn(g_Hooks[i].pRealProxy);
		CBaseEntity *pbe = gameents->EdictToBaseEntity(g_Hooks[i].pEnt);
		if (pbe)
			SH_REMOVE_MANUALHOOK(UpdateOnRemove, pbe, SH_STATIC(Hook_UpdateOnRemove), false);
	}
	SH_REMOVE_HOOK(IServerGameClients, ClientDisconnect, gameclients, SH_STATIC(Hook_ClientDisconnect), false);
	SH_REMOVE_HOOK(IServerGameDLL, GameFrame, gamedll, SH_STATIC(Hook_GameFrame), false);

	plsys->RemovePluginsListener(&g_SendProxyManager);
}

bool SendProxyManager::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	GET_V_IFACE_ANY(GetServerFactory, gameents, IServerGameEnts, INTERFACEVERSION_SERVERGAMEENTS);
	GET_V_IFACE_ANY(GetServerFactory, gameclients, IServerGameClients, INTERFACEVERSION_SERVERGAMECLIENTS);
	GET_V_IFACE_ANY(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	SH_ADD_HOOK(IServerGameDLL, GameFrame, gamedll, SH_STATIC(Hook_GameFrame), false);
	SH_ADD_HOOK(IServerGameClients, ClientDisconnect, gameclients, SH_STATIC(Hook_ClientDisconnect), false);
	return true;
}

void SendProxyManager::OnPluginUnloaded(IPlugin *plugin)
{
	IPluginContext *pCtx = plugin->GetBaseContext();
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].pCallback->GetParentContext() == pCtx)
		{
			UnhookProxy(i);
			i--;
		}
	}
}


bool SendProxyManager::AddHookToList(SendPropHook hook)
{
	//Need to make sure this prop isn't already hooked for this entity
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].pVar == hook.pVar && g_Hooks[i].objectID == hook.objectID)
			return false;
	}
	CBaseEntity *pbe = gameents->EdictToBaseEntity(hook.pEnt);
	SH_ADD_MANUALHOOK(UpdateOnRemove, pbe, SH_STATIC(Hook_UpdateOnRemove), false);
	g_Hooks.AddToTail(hook);
	return true;
}

bool SendProxyManager::HookProxy(SendProp* pProp, int objectID, IPluginFunction *pCallback)
{
	edict_t* pEdict = engine->PEntityOfEntIndex(objectID);
	if (!pEdict || pEdict->IsFree())
		return false;

	SendPropHook hook;
	hook.objectID = objectID;
	hook.pCallback = pCallback;
	hook.PropType = Prop_Int;
	hook.pEnt = pEdict;
	hook.pVar = pProp;
	hook.pRealProxy = pProp->GetProxyFn();
	if (AddHookToList(hook))
		pProp->SetProxyFn(GlobalProxy);
	else
		return false;

	return true;

}

void SendProxyManager::UnhookProxy(int i)
{
	//if there are other hooks for this prop, don't change the proxy, just remove it from our list
	for (int j = 0; j < g_Hooks.Count(); j++)
	{
		if (g_Hooks[j].pVar == g_Hooks[i].pVar && i != j)
		{
			g_Hooks.Remove(i);
			return;
		}
	}
	g_Hooks[i].pVar->SetProxyFn(g_Hooks[i].pRealProxy);
	g_Hooks.Remove(i);
}

bool CallInt(SendPropHook hook, int *ret)
{
	AUTO_LOCK_FM(g_WorkMutex);

	IPluginFunction *callback = hook.pCallback;
	CBaseEntity *pbe = gameents->EdictToBaseEntity(hook.pEnt);
	cell_t value = *ret;
	cell_t result = Pl_Continue;
	callback->PushCell(hook.objectID);
	callback->PushString(hook.pVar->GetName());
	callback->PushCellByRef(&value);
	callback->PushCell(hook.Element);
	callback->Execute(&result);
	if (result == Pl_Changed)
	{
		*ret = value;
		return true;
	}
	return false;
}

bool CallFloat(SendPropHook hook, float *ret)
{
	AUTO_LOCK_FM(g_WorkMutex);

	IPluginFunction *callback = hook.pCallback;
	CBaseEntity *pbe = gameents->EdictToBaseEntity(hook.pEnt);
	float value = *ret;
	cell_t result = Pl_Continue;
	callback->PushCell(hook.objectID);
	callback->PushString(hook.pVar->GetName());
	callback->PushFloatByRef(&value);
	callback->PushCell(hook.Element);
	callback->Execute(&result);
	if (result == Pl_Changed)
	{
		*ret = value;
		return true;
	}
	return false;
}

bool CallString(SendPropHook hook, char **ret)
{
	AUTO_LOCK_FM(g_WorkMutex);

	IPluginFunction *callback = hook.pCallback;
	char value[4096];
	const char *src;
	CBaseEntity *pbe = gameents->EdictToBaseEntity(hook.pEnt);
	src = (char *)((unsigned char*)pbe + hook.Offset);
	strncpy(value, src, sizeof(value));
	cell_t result = Pl_Continue;
	callback->PushCell(hook.objectID);
	callback->PushString(hook.pVar->GetName());
	callback->PushStringEx(value, 4096, SM_PARAM_STRING_UTF8 | SM_PARAM_STRING_COPY, SM_PARAM_COPYBACK);
	callback->PushCell(hook.Element);
	callback->Execute(&result);
	if (result == Pl_Changed)
	{
		*ret = value;
		return true;
	}
	return false;
}

bool CallVector(SendPropHook hook, Vector &vec)
{
	AUTO_LOCK_FM(g_WorkMutex);

	IPluginFunction *callback = hook.pCallback;

	cell_t vector[3];
	vector[0] = sp_ftoc(vec.x);
	vector[1] = sp_ftoc(vec.y);
	vector[2] = sp_ftoc(vec.z);

	cell_t result = Pl_Continue;
	callback->PushCell(hook.objectID);
	callback->PushString(hook.pVar->GetName());
	callback->PushArray(vector, 3, SM_PARAM_COPYBACK);
	callback->PushCell(hook.Element);
	callback->Execute(&result);
	if (result == Pl_Changed)
	{
		vec.x = sp_ctof(vector[0]);
		vec.y = sp_ctof(vector[1]);
		vec.z = sp_ctof(vector[2]);
		return true;
	}
	return false;
}

void GlobalProxy(const SendProp *pProp, const void *pStructBase, const void* pData, DVariant *pOut, int iElement, int objectID)
{
	edict_t* pEnt = engine->PEntityOfEntIndex(objectID);
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].objectID == objectID && g_Hooks[i].pVar == pProp && pEnt == g_Hooks[i].pEnt)
		{
			switch (g_Hooks[i].PropType)
			{
			case Prop_Int:
				{
					int result = *(int *)pData;

					if (CallInt(g_Hooks[i], &result))
					{
						long data = result;

						g_Hooks[i].pRealProxy(pProp, pStructBase, &data, pOut, iElement, objectID);
						return;
					} else {
						g_Hooks[i].pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
						return;
					}
				}
			case Prop_Float:
				{
					float result = *(float *)pData;

					if (CallFloat(g_Hooks[i], &result))
					{
						g_Hooks[i].pRealProxy(pProp, pStructBase, &result, pOut, iElement, objectID);
						return;
					} else {
						g_Hooks[i].pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
						return;
					}
				}
			case Prop_String:
				{
					char* result = (char *)pData;

					if (CallString(g_Hooks[i], &result))
					{
						g_Hooks[i].pRealProxy(pProp, pStructBase, &result, pOut, iElement, objectID);
						return;
					} else {
						g_Hooks[i].pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
						return;
					}
				}
			case Prop_Vector:
				{
					Vector result = *(Vector *)pData;

					if (CallVector(g_Hooks[i], result))
					{
						g_Hooks[i].pRealProxy(pProp, pStructBase, &result, pOut, iElement, objectID);
						return;
					} else {
						g_Hooks[i].pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
						return;
					}
				}
			default: printf("wat do?\n");
			}
		}
	}
	//perhaps we aren't hooked, but we can still find the real proxy for this prop
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].pVar == pProp)
		{
			g_Hooks[i].pRealProxy(pProp, pStructBase, pData, pOut, iElement, objectID);
			return;
		}
	}
	g_pSM->LogError(myself, "CRITICAL: Proxy for unmanaged entity %d called for prop %s", objectID, pProp->GetName());
	
}

//PropChanged(entity, const String:propname[], const String:oldValue[], const String:newValue[])
//SendProxy_HookPropChange(entity, const String:name[], PropChanged:callback)

static cell_t Native_UnhookPropChange(IPluginContext* pContext, const cell_t* params)
{
	if (params[1] < 0 || params[1] >= 2048)
	{
		return pContext->ThrowNativeError("Invalid Edict Index %d", params[1]);
	}
	int entity = params[1];
	char* name;
	edict_t* pEnt = engine->PEntityOfEntIndex(entity);
	pContext->LocalToString(params[2], &name);
	IPluginFunction *callback = pContext->GetFunctionById(params[3]);
	sm_sendprop_info_t info;
	ServerClass *sc = pEnt->GetNetworkable()->GetServerClass();
	gamehelpers->FindSendPropInfo(sc->GetName(), name, &info);
	
	for (int i = 0; i < g_ChangeHooks.Count(); i++)
	{
		if (g_ChangeHooks[i].pCallback == callback && g_ChangeHooks[i].objectID == entity && g_ChangeHooks[i].pVar == info.prop)
			g_ChangeHooks.Remove(i--);
	}
	return 1;
}
	
static cell_t Native_HookPropChange(IPluginContext* pContext, const cell_t* params)
{
	if (params[1] < 0 || params[1] >= 2048)
	{
		return pContext->ThrowNativeError("Invalid Edict Index %d", params[1]);
	}
	int entity = params[1];
	char* name;
	edict_t* pEnt = engine->PEntityOfEntIndex(entity);
	pContext->LocalToString(params[2], &name);
	IPluginFunction *callback = pContext->GetFunctionById(params[3]);
	SendProp *pProp = NULL;
	PropChangeHook hook;
	sm_sendprop_info_t info;
	ServerClass *sc = pEnt->GetNetworkable()->GetServerClass();
	gamehelpers->FindSendPropInfo(sc->GetName(), name, &info);

	pProp = info.prop;
	int offset = info.actual_offset;
	SendPropType type = pProp->GetType();
	CBaseEntity* pEntity = gameents->EdictToBaseEntity(pEnt);

	switch (type)
	{
	case DPT_Int: hook.PropType = Prop_Int; hook.iLastValue = *(int*)((unsigned char*)pEntity + offset); break;
	case DPT_Float: hook.PropType = Prop_Float; hook.flLastValue = *(float*)((unsigned char*)pEntity + offset); break;
	case DPT_String: hook.PropType = Prop_String; hook.szLastValue = *(const char*)((unsigned char*)pEntity + offset); break;
	default: return pContext->ThrowNativeError("Prop type %d is not yet supported", type);
	}

	hook.objectID = entity;
	hook.Offset = offset;
	hook.pVar = pProp;
	hook.pCallback = callback;

	g_ChangeHooks.AddToTail(hook);
	return 1;
}

static cell_t Native_Hook(IPluginContext* pContext, const cell_t* params)
{
	if (params[1] < 0 || params[1] >= 2048)
	{
		return pContext->ThrowNativeError("Invalid Edict Index %d", params[1]);
	}
	int entity = params[1];
	char* name;
	pContext->LocalToString(params[2], &name);
	edict_t* pEnt = engine->PEntityOfEntIndex(entity);
	int propType = params[3];
	IPluginFunction *callback = pContext->GetFunctionById(params[4]);
	SendProp *pProp = NULL;
	ServerClass *sc = pEnt->GetNetworkable()->GetServerClass();
	sm_sendprop_info_t info;
	
	if (!sc)
	{
		pContext->ThrowNativeError("Cannot find ServerClass for entity %d", params[1]);
		return 0;
	}

	gamehelpers->FindSendPropInfo(sc->GetName(), name, &info);
	pProp = info.prop;
	if (!pProp)
	{
		pContext->ThrowNativeError("Could not find prop %s", name);
		return 0;
	}
	switch (propType)
	{
	case Prop_Int: 
		{
			if (pProp->GetType() != DPT_Int)
				return pContext->ThrowNativeError("Prop %s is not an int!", pProp->GetName());
			break;
		}
	case Prop_Float:
		{
			if (pProp->GetType() != DPT_Float)
				return pContext->ThrowNativeError("Prop %s is not a float!", pProp->GetName());
			break;
		}
	case Prop_String:
		{
			if (pProp->GetType() != DPT_String)
				return pContext->ThrowNativeError("Prop %s is not a string!", pProp->GetName());
			break;
		}
	case Prop_Vector:
		{
			if (pProp->GetType() != DPT_Vector)
				return pContext->ThrowNativeError("Prop %s is not a vector!", pProp->GetName());
			break;
		}
	default: return pContext->ThrowNativeError("Unsupported prop type %d", propType);
	}
	
	
	SendPropHook hook;
	hook.objectID = entity;
	hook.pCallback = callback;
	hook.pEnt = pEnt;
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].pVar == pProp)
		{
			hook.pRealProxy = g_Hooks[i].pRealProxy;
			goto after;
		}
	}
	hook.pRealProxy = pProp->GetProxyFn();
after:
	hook.PropType = propType;
	hook.pVar = pProp;
	hook.Offset = info.actual_offset;
	
	//if this prop has been hooked already, don't set the proxy again
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].pVar == pProp)
		{
			hook.pRealProxy = g_Hooks[i].pRealProxy;
			g_SendProxyManager.AddHookToList(hook);
			return 1;
		}
	}
	g_SendProxyManager.AddHookToList(hook);
	pProp->SetProxyFn(GlobalProxy);
	return 1;
}


//native SendProxy_HookArrayProp(entity, const String:name[], element, SendPropType:type, SendProxyCallback:callback);

static cell_t Native_HookArrayProp(IPluginContext* pContext, const cell_t* params)
{
	if (params[1] < 0 || params[1] >= 2048)
	{
		return pContext->ThrowNativeError("Invalid Edict Index %d", params[1]);
	}
	int entity = params[1];
	char *propName;
	pContext->LocalToString(params[2], &propName);
	int element = params[3];
	int propType = params[4];
	IPluginFunction *callback = pContext->GetFunctionById(params[5]);
	
	edict_t* pEnt = engine->PEntityOfEntIndex(entity);
	ServerClass *sc = pEnt->GetNetworkable()->GetServerClass();
	sm_sendprop_info_t info;
	gamehelpers->FindSendPropInfo(sc->GetName(), propName, &info);
	if (!info.prop)
	{
		return pContext->ThrowNativeError("Could not find prop %s", propName);
	}
	SendTable *st = info.prop->GetDataTable();
	if (!st)
	{
		return pContext->ThrowNativeError("Prop %s does not contain any elements", propName);
	}

	SendProp *pProp = st->GetProp(element);
	if (!pProp)
	{
		return pContext->ThrowNativeError("Could not find element %d in %s", element, info.prop->GetName());
	}
	
	SendPropHook hook;
	hook.objectID = entity;
	hook.pCallback = callback;
	hook.pEnt = pEnt;
	hook.Element = element;
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].pVar == pProp)
		{
			hook.pRealProxy = g_Hooks[i].pRealProxy;
			goto after1;
		}
	}
	hook.pRealProxy = pProp->GetProxyFn();
after1:
	hook.PropType = propType;
	hook.pVar = pProp;
	
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].pVar == pProp)
		{
			hook.pRealProxy = g_Hooks[i].pRealProxy;
			g_SendProxyManager.AddHookToList(hook);
			return 1;
		}
	}
	g_SendProxyManager.AddHookToList(hook);
	pProp->SetProxyFn(GlobalProxy);
	return 1;
}

//native SendProxy_UnhookArrayProp(entity, const String:name[], element, SendPropType:type, SendProxyCallback:callback);

static cell_t Native_UnhookArrayProp(IPluginContext* pContext, const cell_t* params)
{
	if (params[1] < 0 || params[1] >= 2048)
	{
		return pContext->ThrowNativeError("Invalid Edict Index %d", params[1]);
	}
	int entity = params[1];
	char *propName;
	pContext->LocalToString(params[2], &propName);
	int element = params[3];
	int propType = params[4];
	IPluginFunction *callback = pContext->GetFunctionById(params[5]);
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].Element == element && g_Hooks[i].PropType == propType && g_Hooks[i].pCallback == callback && !strcmp(g_Hooks[i].pVar->GetName(), propName) && g_Hooks[i].objectID == entity)
		{
			g_SendProxyManager.UnhookProxy(i);
		}
	}
	return 0;
}

static cell_t Native_Unhook(IPluginContext* pContext, const cell_t* params)
{
	char *propName;
	pContext->LocalToString(params[2], &propName);
	IPluginFunction *pFunction = pContext->GetFunctionById(params[3]);
	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (params[1] == g_Hooks[i].objectID && strcmp(g_Hooks[i].pVar->GetName(), propName) == 0 && pFunction == g_Hooks[i].pCallback)
		{
			g_SendProxyManager.UnhookProxy(i);
			return 1;
		}
	}
	return 0;
}

static cell_t Native_IsHooked(IPluginContext* pContext, const cell_t* params)
{
	int objectID = params[1];
	char *propName;
	pContext->LocalToString(params[2], &propName);

	for (int i = 0; i < g_Hooks.Count(); i++)
	{
		if (g_Hooks[i].objectID == objectID && strcmp(propName, g_Hooks[i].pVar->GetName()) == 0)
			return 1;
	}
	return 0;
}