Scriptname ALife_MCM extends SKI_ConfigBase
{A-Life for Skyrim (Anomaly edition) - SkyUI MCM. Debug settings are ini-only.}

; Filled by ALife.esp (MESG 0x945): one vanilla Yes/No message box.
Message Property UninstallMessage Auto

; Native bridge implemented in ALife.dll. Setters are queued to the main thread by the DLL.
Bool Function ALifeGetEnabled() Global Native
Function ALifeSetEnabled(Bool abOn) Global Native
Float Function ALifeGetPopulationScale() Global Native
Function ALifeSetPopulationScale(Float afScale) Global Native
; difficulty (docs/DIFFICULTY.md): channel 0 population, 1 monsters, 2 squad strength, 3 combat lethality, 4 economy,
; 5 wounded and surrender, 6 task difficulty, 7 dragons
Float Function ALifeGetDiffScale(Int aiChannel) Global Native
Function ALifeSetDiffScale(Int aiChannel, Float afScale) Global Native
Float Function ALifeGetDiffMin(Int aiChannel) Global Native
Float Function ALifeGetDiffMax(Int aiChannel) Global Native
String Function ALifeDiffEffectiveText(Int aiChannel) Global Native
Bool Function ALifeGetFollowGame() Global Native
Function ALifeSetFollowGame(Bool abOn) Global Native
String Function ALifeGameDifficulty() Global Native
Int Function ALifeGetOfflineCombat() Global Native
Function ALifeSetOfflineCombat(Int aiMode) Global Native
Bool Function ALifeGetNews() Global Native
Function ALifeSetNews(Bool abOn) Global Native
Bool Function ALifeGetDynamicRelations() Global Native
Function ALifeSetDynamicRelations(Bool abOn) Global Native
Bool Function ALifeGetDragons() Global Native
Function ALifeSetDragons(Bool abOn) Global Native
Int Function ALifeGetCompatBudget() Global Native
Function ALifeSetCompatBudget(Int aiBudget) Global Native
String Function ALifeCompatInfo() Global Native
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
Int OID_Follow
Int[] OID_Diff
String[] sDiffNames
String[] sDiffInfo
Bool bFollowShown = true
Int OID_Combat
Int OID_News
Int OID_DynRel
Int OID_Dragons
Int OID_Talk
Int OID_Compat
Int OID_Loot
Int OID_Status
Int OID_Uninstall
Bool bEnabledShown = true
Bool bNewsShown = true
Bool bDynRelShown = false
Bool bDragonsShown = true
Bool bLootShown = true
Int iTalkShown = 2
String[] sTalkModes
Int iCompatShown = 0
String[] sCompatModes
Int iCombatShown = 1
String[] sCombatModes

Int Function GetVersion()
	Return 205
EndFunction

Event OnConfigInit()
	ModName = "A-Life"
	Pages = new String[4]
	Pages[0] = "A-Life"
	Pages[1] = "Difficulty"
	Pages[2] = "Standing"
	Pages[3] = "News"
	OID_Diff = new Int[8]
	sDiffNames = new String[8]
	sDiffNames[0] = "Population density"
	sDiffNames[1] = "Monster population"
	sDiffNames[2] = "Squad strength"
	sDiffNames[3] = "Offline combat lethality"
	sDiffNames[4] = "Economy (rewards)"
	sDiffNames[5] = "Wounded and surrender"
	sDiffNames[6] = "Task difficulty"
	sDiffNames[7] = "Dragons"
	sDiffInfo = new String[8]
	sDiffInfo[0] = "How many squads live in the world (Anomaly's stalker population). The on-screen budget never changes."
	sDiffInfo[1] = "How many monster packs and bands fill the lairs (Anomaly's mutant population)."
	sDiffInfo[2] = "Bigger squads, higher levels and better gear for everyone the world makes (at most 7 to a squad)."
	sDiffInfo[3] = "How deadly fights between squads are while you are far away."
	sDiffInfo[4] = "Job rewards (and hidden stash value) go up with it; tolls and companion pay go down."
	sDiffInfo[5] = "How often the beaten go down wounded, yield or surrender instead of dying, and how rarely they are finished off."
	sDiffInfo[6] = "Jobs ask for more: bigger waves to hold off, more ambushes on escorts, tougher targets."
	sDiffInfo[7] = "How often dragons fly over the world and how strong they are."
	sCombatModes = new String[3]
	sCombatModes[0] = "Off"
	sCombatModes[1] = "At places (default)"
	sCombatModes[2] = "Everywhere"
	sTalkModes = new String[4]
	sTalkModes[0] = "Never (only when I talk to them)"
	sTalkModes[1] = "Rarely"
	sTalkModes[2] = "Normally (default)"
	sTalkModes[3] = "Often"
	sCompatModes = new String[5]
	sCompatModes[0] = "Auto (default)"
	sCompatModes[1] = "Off (full A-Life)"
	sCompatModes[2] = "1 squad"
	sCompatModes[3] = "2 squads"
	sCompatModes[4] = "3 squads"
	Debug.Trace("[ALife] MCM registered (version " + GetVersion() + ")")
EndEvent

Event OnVersionUpdate(Int aiVersion)
	If aiVersion > 1
		OnConfigInit()
	EndIf
EndEvent

Event OnPageReset(String asPage)
	SetCursorFillMode(TOP_TO_BOTTOM)
	If asPage == "Difficulty"
		ShowDifficulty()
		Return
	ElseIf asPage == "Standing"
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
	iCombatShown = ALifeGetOfflineCombat()
	bNewsShown = ALifeGetNews()
	bDynRelShown = ALifeGetDynamicRelations()
	bDragonsShown = ALifeGetDragons()
	iTalkShown = ALifeGetTalkInitiative()
	bLootShown = ALifeGetLoot()
	iCompatShown = ALifeGetCompatBudget() + 1
	AddHeaderOption("Simulation")
	OID_Enabled = AddToggleOption("A-Life enabled", bEnabledShown, flags)
	OID_Combat = AddMenuOption("Offline combat", sCombatModes[iCombatShown], flags)
	OID_News = AddToggleOption("News notifications", bNewsShown, flags)
	OID_DynRel = AddToggleOption("Dynamic faction relations", bDynRelShown, flags)
	OID_Dragons = AddToggleOption("Dragons", bDragonsShown, flags)
	AddHeaderOption("Encounters")
	OID_Talk = AddMenuOption("Squads that stop you to talk", sTalkModes[iTalkShown], flags)
	OID_Loot = AddToggleOption("Squads loot bodies", bLootShown, flags)
	AddHeaderOption("World")
	OID_Status = AddTextOption("Status", ALifeStatus(), OPTION_FLAG_DISABLED)
	AddTextOption("Patrol mods", ALifeCompatInfo(), OPTION_FLAG_DISABLED)
	OID_Compat = AddMenuOption("On screen with patrol mods", sCompatModes[iCompatShown], flags)
	OID_Uninstall = AddTextOption("Prepare to uninstall", "Press", flags)
EndEvent

Function ShowDifficulty()
	Int flags = OPTION_FLAG_NONE
	If !ALifeReady()
		flags = OPTION_FLAG_DISABLED
	EndIf
	bFollowShown = ALifeGetFollowGame()
	AddHeaderOption("Skyrim difficulty")
	AddTextOption("Current", ALifeGameDifficulty(), OPTION_FLAG_DISABLED)
	OID_Follow = AddToggleOption("Follow game difficulty", bFollowShown, flags)
	AddHeaderOption("A-Life sliders (x the difficulty)")
	Int i = 0
	While i < 8
		OID_Diff[i] = AddSliderOption(sDiffNames[i], ALifeGetDiffScale(i), "{1}x", flags)
		i += 1
	EndWhile
	SetCursorPosition(1)
	AddHeaderOption("In effect now")
	i = 0
	While i < 8
		AddTextOption(sDiffNames[i], ALifeDiffEffectiveText(i), OPTION_FLAG_DISABLED)
		i += 1
	EndWhile
EndFunction

Int Function DiffIndex(Int aiOption)
	Int i = 0
	While i < 8
		If OID_Diff[i] == aiOption
			Return i
		EndIf
		i += 1
	EndWhile
	Return -1
EndFunction

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
	If aiOption == OID_Follow
		bFollowShown = !bFollowShown
		ALifeSetFollowGame(bFollowShown)
		SetToggleOptionValue(OID_Follow, bFollowShown)
		ForcePageReset()
	ElseIf aiOption == OID_Enabled
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
	ElseIf aiOption == OID_Dragons
		bDragonsShown = !bDragonsShown
		ALifeSetDragons(bDragonsShown)
		SetToggleOptionValue(OID_Dragons, bDragonsShown)
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
	ElseIf aiOption == OID_Compat
		SetMenuDialogStartIndex(iCompatShown)
		SetMenuDialogDefaultIndex(0)
		SetMenuDialogOptions(sCompatModes)
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
	ElseIf aiOption == OID_Compat
		iCompatShown = aiIndex
		ALifeSetCompatBudget(aiIndex - 1)
		SetMenuOptionValue(OID_Compat, sCompatModes[aiIndex])
	EndIf
EndEvent

Event OnOptionSliderOpen(Int aiOption)
	Int d = DiffIndex(aiOption)
	If d >= 0
		SetSliderDialogStartValue(ALifeGetDiffScale(d))
		SetSliderDialogDefaultValue(1.0)
		SetSliderDialogRange(ALifeGetDiffMin(d), ALifeGetDiffMax(d))
		SetSliderDialogInterval(0.1)
	EndIf
EndEvent

Event OnOptionSliderAccept(Int aiOption, Float afValue)
	Int d = DiffIndex(aiOption)
	If d >= 0
		ALifeSetDiffScale(d, afValue)
		SetSliderOptionValue(aiOption, afValue, "{1}x")
	EndIf
EndEvent

Event OnOptionDefault(Int aiOption)
	Int d = DiffIndex(aiOption)
	If d >= 0
		ALifeSetDiffScale(d, 1.0)
		SetSliderOptionValue(aiOption, 1.0, "{1}x")
	ElseIf aiOption == OID_Follow
		bFollowShown = true
		ALifeSetFollowGame(true)
		SetToggleOptionValue(OID_Follow, true)
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
	ElseIf aiOption == OID_Dragons
		bDragonsShown = true
		ALifeSetDragons(true)
		SetToggleOptionValue(OID_Dragons, true)
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
	ElseIf aiOption == OID_Compat
		iCompatShown = 0
		ALifeSetCompatBudget(-1)
		SetMenuOptionValue(OID_Compat, sCompatModes[0])
	EndIf
EndEvent

Event OnOptionHighlight(Int aiOption)
	Int d = DiffIndex(aiOption)
	If d >= 0
		SetInfoText(sDiffInfo[d] + " Multiplies the row for Skyrim's difficulty in difficulty.ltx (Adept = 1).")
	ElseIf aiOption == OID_Follow
		SetInfoText("On: Skyrim's difficulty (Novice .. Legendary) also scales A-Life: fewer, weaker squads and richer jobs on Novice, more and stronger squads and leaner pay on Legendary. Off: only the sliders count.")
	ElseIf aiOption == OID_Enabled
		SetInfoText("Off sends every A-Life squad offline and pauses the simulation clock.")
	ElseIf aiOption == OID_Combat
		SetInfoText("Anomaly's offline combat simulator: enemy squads that meet while you are far away fight it out. 'At places' = only squads heading to the same place; 'Everywhere' = any two that cross paths.")
	ElseIf aiOption == OID_News
		SetInfoText("Short notices about events near you: calls for help, battle and death reports, squads wiped out, forts changing hands. Everything, near or far, is kept on the News page.")
	ElseIf aiOption == OID_DynRel
		SetInfoText("Anomaly's dynamic faction relations (off by default, as in Anomaly): every death shifts how the factions see each other; wars, alliances and cease-fires are announced. The Civil War stays a war.")
	ElseIf aiOption == OID_Dragons
		SetInfoText("Dragons fly between their lairs, word walls and the high country and hunt the squads they find (offline and on screen). None before the dragons return in the main quest. Off: no A-Life dragons (vanilla dragons are not touched).")
	ElseIf aiOption == OID_Talk
		SetInfoText("How often a squad walks up and stops you to talk (a bandit's toll, a patrol's challenge). 'Never' leaves every conversation to you: talk to them yourself. After any conversation there is always a pause before the next.")
	ElseIf aiOption == OID_Loot
		SetInfoText("Squads strip the bodies they pass (their own fallen, their enemies, and bodies you left behind after a while). Quest items are never taken.")
	ElseIf aiOption == OID_Compat
		SetInfoText("Immersive Patrols / Skyrim at War put their own soldiers on the roads and in the forts. 'Auto' shrinks A-Life's on-screen squads and population while they are loaded; 'Off' keeps A-Life's full 3 squads / 15 actors; or pick a fixed number of squads (5 actors each). Their forts are left to their garrisons either way.")
	ElseIf aiOption == OID_Uninstall
		SetInfoText("Removes every A-Life actor from the world and clears A-Life state so the mod can be removed safely.")
	EndIf
EndEvent
