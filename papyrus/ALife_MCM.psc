Scriptname ALife_MCM extends SKI_ConfigBase
{A-Life for Skyrim (Anomaly edition) - SkyUI MCM. Debug settings are ini-only.}

; Filled by ALife.esp (MESG 0x945): one vanilla Yes/No message box.
Message Property UninstallMessage Auto

; Native bridge implemented in ALife.dll. Setters are queued to the main thread by the DLL.
Bool Function ALifeGetEnabled() Global Native
Function ALifeSetEnabled(Bool abOn) Global Native
Float Function ALifeGetPopulationScale() Global Native
Function ALifeSetPopulationScale(Float afScale) Global Native
Int Function ALifeGetOfflineCombat() Global Native
Function ALifeSetOfflineCombat(Int aiMode) Global Native
Bool Function ALifeGetNews() Global Native
Function ALifeSetNews(Bool abOn) Global Native
Bool Function ALifeGetDynamicRelations() Global Native
Function ALifeSetDynamicRelations(Bool abOn) Global Native
Int Function ALifeGetTalkInitiative() Global Native
Function ALifeSetTalkInitiative(Int aiMode) Global Native
Bool Function ALifeGetLoot() Global Native
Function ALifeSetLoot(Bool abOn) Global Native
Function ALifePrepareUninstall() Global Native
Bool Function ALifeReady() Global Native
String Function ALifeStatus() Global Native
Int Function ALifeStandingCount() Global Native
String Function ALifeStandingName(Int aiIndex) Global Native
String Function ALifeStandingValue(Int aiIndex) Global Native
String Function ALifeStandingStatus() Global Native
Int Function ALifeNewsCount() Global Native
String Function ALifeNewsLine(Int aiIndex) Global Native

Int OID_Enabled
Int OID_PopScale
Int OID_Combat
Int OID_News
Int OID_DynRel
Int OID_Talk
Int OID_Loot
Int OID_Status
Int OID_Uninstall
Bool bEnabledShown = true
Bool bNewsShown = true
Bool bDynRelShown = false
Bool bLootShown = true
Int iTalkShown = 2
String[] sTalkModes
Float fScaleShown = 1.0
Int iCombatShown = 1
String[] sCombatModes

Int Function GetVersion()
	Return 203
EndFunction

Event OnConfigInit()
	ModName = "A-Life"
	Pages = new String[3]
	Pages[0] = "A-Life"
	Pages[1] = "Standing"
	Pages[2] = "News"
	sCombatModes = new String[3]
	sCombatModes[0] = "Off"
	sCombatModes[1] = "At places (default)"
	sCombatModes[2] = "Everywhere"
	sTalkModes = new String[4]
	sTalkModes[0] = "Never (only when I talk to them)"
	sTalkModes[1] = "Rarely"
	sTalkModes[2] = "Normally (default)"
	sTalkModes[3] = "Often"
	Debug.Trace("[ALife] MCM registered (version " + GetVersion() + ")")
EndEvent

Event OnVersionUpdate(Int aiVersion)
	If aiVersion > 1
		OnConfigInit()
	EndIf
EndEvent

Event OnPageReset(String asPage)
	SetCursorFillMode(TOP_TO_BOTTOM)
	If asPage == "Standing"
		ShowStanding()
		Return
	ElseIf asPage == "News"
		ShowNews()
		Return
	EndIf
	Int flags = OPTION_FLAG_NONE
	If !ALifeReady()
		flags = OPTION_FLAG_DISABLED
	EndIf
	bEnabledShown = ALifeGetEnabled()
	fScaleShown = ALifeGetPopulationScale()
	iCombatShown = ALifeGetOfflineCombat()
	bNewsShown = ALifeGetNews()
	bDynRelShown = ALifeGetDynamicRelations()
	iTalkShown = ALifeGetTalkInitiative()
	bLootShown = ALifeGetLoot()
	AddHeaderOption("Simulation")
	OID_Enabled = AddToggleOption("A-Life enabled", bEnabledShown, flags)
	OID_PopScale = AddSliderOption("Population density", fScaleShown, "{1}x", flags)
	OID_Combat = AddMenuOption("Offline combat", sCombatModes[iCombatShown], flags)
	OID_News = AddToggleOption("News notifications", bNewsShown, flags)
	OID_DynRel = AddToggleOption("Dynamic faction relations", bDynRelShown, flags)
	AddHeaderOption("Encounters")
	OID_Talk = AddMenuOption("Squads that stop you to talk", sTalkModes[iTalkShown], flags)
	OID_Loot = AddToggleOption("Squads loot bodies", bLootShown, flags)
	AddHeaderOption("World")
	OID_Status = AddTextOption("Status", ALifeStatus(), OPTION_FLAG_DISABLED)
	OID_Uninstall = AddTextOption("Prepare to uninstall", "Press", flags)
EndEvent

Function ShowNews()
	SetCursorFillMode(LEFT_TO_RIGHT)
	AddHeaderOption("News of the road (newest first)")
	AddEmptyOption()
	Int n = ALifeNewsCount()
	If n == 0
		AddTextOption("Nothing yet", "", OPTION_FLAG_DISABLED)
	EndIf
	Int i = 0
	While i < n && i < 24
		AddTextOption(ALifeNewsLine(i), "", OPTION_FLAG_DISABLED)
		i += 1
	EndWhile
EndFunction

Function ShowStanding()
	AddHeaderOption("How the people of Skyrim see you")
	Int n = ALifeStandingCount()
	Int i = 0
	While i < n
		AddTextOption(ALifeStandingName(i), ALifeStandingValue(i), OPTION_FLAG_DISABLED)
		i += 1
	EndWhile
	SetCursorPosition(1)
	AddHeaderOption("What they know")
	AddTextOption("Record", ALifeStandingStatus(), OPTION_FLAG_DISABLED)
EndFunction

Event OnOptionSelect(Int aiOption)
	If aiOption == OID_Enabled
		bEnabledShown = !bEnabledShown
		ALifeSetEnabled(bEnabledShown)
		SetToggleOptionValue(OID_Enabled, bEnabledShown)
	ElseIf aiOption == OID_News
		bNewsShown = !bNewsShown
		ALifeSetNews(bNewsShown)
		SetToggleOptionValue(OID_News, bNewsShown)
	ElseIf aiOption == OID_DynRel
		bDynRelShown = !bDynRelShown
		ALifeSetDynamicRelations(bDynRelShown)
		SetToggleOptionValue(OID_DynRel, bDynRelShown)
	ElseIf aiOption == OID_Loot
		bLootShown = !bLootShown
		ALifeSetLoot(bLootShown)
		SetToggleOptionValue(OID_Loot, bLootShown)
	ElseIf aiOption == OID_Uninstall
		; One vanilla message box: button 0 = Yes, 1 = No.
		If UninstallMessage && UninstallMessage.Show() == 0
			ALifePrepareUninstall()
			SetTextOptionValue(OID_Uninstall, "Done - save, then remove")
		EndIf
	EndIf
EndEvent

Event OnOptionMenuOpen(Int aiOption)
	If aiOption == OID_Combat
		SetMenuDialogStartIndex(iCombatShown)
		SetMenuDialogDefaultIndex(1)
		SetMenuDialogOptions(sCombatModes)
	ElseIf aiOption == OID_Talk
		SetMenuDialogStartIndex(iTalkShown)
		SetMenuDialogDefaultIndex(2)
		SetMenuDialogOptions(sTalkModes)
	EndIf
EndEvent

Event OnOptionMenuAccept(Int aiOption, Int aiIndex)
	If aiOption == OID_Combat
		iCombatShown = aiIndex
		ALifeSetOfflineCombat(aiIndex)
		SetMenuOptionValue(OID_Combat, sCombatModes[aiIndex])
	ElseIf aiOption == OID_Talk
		iTalkShown = aiIndex
		ALifeSetTalkInitiative(aiIndex)
		SetMenuOptionValue(OID_Talk, sTalkModes[aiIndex])
	EndIf
EndEvent

Event OnOptionSliderOpen(Int aiOption)
	If aiOption == OID_PopScale
		SetSliderDialogStartValue(fScaleShown)
		SetSliderDialogDefaultValue(1.0)
		SetSliderDialogRange(0.5, 1.5)
		SetSliderDialogInterval(0.1)
	EndIf
EndEvent

Event OnOptionSliderAccept(Int aiOption, Float afValue)
	If aiOption == OID_PopScale
		fScaleShown = afValue
		ALifeSetPopulationScale(afValue)
		SetSliderOptionValue(OID_PopScale, afValue, "{1}x")
	EndIf
EndEvent

Event OnOptionDefault(Int aiOption)
	If aiOption == OID_PopScale
		fScaleShown = 1.0
		ALifeSetPopulationScale(1.0)
		SetSliderOptionValue(OID_PopScale, 1.0, "{1}x")
	ElseIf aiOption == OID_Enabled
		bEnabledShown = true
		ALifeSetEnabled(true)
		SetToggleOptionValue(OID_Enabled, true)
	ElseIf aiOption == OID_News
		bNewsShown = true
		ALifeSetNews(true)
		SetToggleOptionValue(OID_News, true)
	ElseIf aiOption == OID_DynRel
		bDynRelShown = false
		ALifeSetDynamicRelations(false)
		SetToggleOptionValue(OID_DynRel, false)
	ElseIf aiOption == OID_Combat
		iCombatShown = 1
		ALifeSetOfflineCombat(1)
		SetMenuOptionValue(OID_Combat, sCombatModes[1])
	ElseIf aiOption == OID_Talk
		iTalkShown = 2
		ALifeSetTalkInitiative(2)
		SetMenuOptionValue(OID_Talk, sTalkModes[2])
	ElseIf aiOption == OID_Loot
		bLootShown = true
		ALifeSetLoot(true)
		SetToggleOptionValue(OID_Loot, true)
	EndIf
EndEvent

Event OnOptionHighlight(Int aiOption)
	If aiOption == OID_Enabled
		SetInfoText("Off sends every A-Life squad offline and pauses the simulation clock.")
	ElseIf aiOption == OID_PopScale
		SetInfoText("How many squads live in the world (Anomaly's stalker population). The on-screen budget never changes.")
	ElseIf aiOption == OID_Combat
		SetInfoText("Anomaly's offline combat simulator: enemy squads that meet while you are far away fight it out. 'At places' = only squads heading to the same place; 'Everywhere' = any two that cross paths.")
	ElseIf aiOption == OID_News
		SetInfoText("Short notices about events near you: calls for help, battle and death reports, squads wiped out, forts changing hands. Everything, near or far, is kept on the News page.")
	ElseIf aiOption == OID_DynRel
		SetInfoText("Anomaly's dynamic faction relations (off by default, as in Anomaly): every death shifts how the factions see each other; wars, alliances and cease-fires are announced. The Civil War stays a war.")
	ElseIf aiOption == OID_Talk
		SetInfoText("How often a squad walks up and stops you to talk (a bandit's toll, a patrol's challenge). 'Never' leaves every conversation to you: talk to them yourself. After any conversation there is always a pause before the next.")
	ElseIf aiOption == OID_Loot
		SetInfoText("Squads strip the bodies they pass (their own fallen, their enemies, and bodies you left behind after a while). Quest items are never taken.")
	ElseIf aiOption == OID_Uninstall
		SetInfoText("Removes every A-Life actor from the world and clears A-Life state so the mod can be removed safely.")
	EndIf
EndEvent
