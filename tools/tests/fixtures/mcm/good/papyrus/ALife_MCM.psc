Scriptname ALife_MCM extends SKI_ConfigBase
; SYNTHETIC FIXTURE for tools/tests/test_mcm_check.py: a cut-down MCM with the shape tools/mcm_check.py parses.

Bool Function ALifeGetEnabled() Global Native
Function ALifeSetEnabled(Bool abOn) Global Native
Function ALifeSetNews(Bool abOn) Global Native
Function ALifeSetDynamicRelations(Bool abOn) Global Native
Function ALifeSetLoot(Bool abOn) Global Native
Function ALifeSetOfflineCombat(Int aiMode) Global Native
Function ALifeSetTalkInitiative(Int aiMode) Global Native
Function ALifeSetCompatBudget(Int aiBudget) Global Native
Function ALifeSetFollowGame(Bool abOn) Global Native
Float Function ALifeGetDiffScale(Int aiChannel) Global Native
Function ALifeSetDiffScale(Int aiChannel, Float afScale) Global Native
String Function ALifeStatus() Global Native
Function ALifePrepareUninstall() Global Native

Int OID_Enabled
Int OID_News
Int OID_DynRel
Int OID_Loot
Int OID_Combat
Int OID_Talk
Int OID_Compat
Int OID_Follow
Int OID_Uninstall
Int OID_Status
Int[] OID_Diff

Event OnConfigInit()
	Pages = new String[2]
	Pages[0] = "A-Life"
	Pages[1] = "Difficulty"
	OID_Diff = new Int[2]
EndEvent

Event OnPageReset(String asPage)
	If asPage == "Difficulty"
		OID_Follow = AddToggleOption("Follow game difficulty", ALifeGetEnabled(), 0)
		Int i = 0
		While i < 2
			OID_Diff[i] = AddSliderOption("Slider", ALifeGetDiffScale(i), "{1}x", 0)
			i += 1
		EndWhile
		Return
	EndIf
	OID_Enabled = AddToggleOption("A-Life enabled", true, 0)
	OID_News = AddToggleOption("News", true, 0)
	OID_DynRel = AddToggleOption("Dynamic relations", false, 0)
	OID_Loot = AddToggleOption("Loot", true, 0)
	OID_Combat = AddMenuOption("Offline combat", "At places", 0)
	OID_Talk = AddMenuOption("Talk", "Normally", 0)
	OID_Compat = AddMenuOption("Patrol mods", "Auto", 0)
	OID_Status = AddTextOption("Status", ALifeStatus(), OPTION_FLAG_DISABLED)
	OID_Uninstall = AddTextOption("Prepare to uninstall", "Press", 0)
EndEvent

Int Function DiffIndex(Int aiOption)
	Return -1
EndFunction

Event OnOptionSelect(Int aiOption)
	If aiOption == OID_Follow
		ALifeSetFollowGame(false)
	ElseIf aiOption == OID_Enabled
		ALifeSetEnabled(false)
	ElseIf aiOption == OID_News
		ALifeSetNews(false)
	ElseIf aiOption == OID_DynRel
		ALifeSetDynamicRelations(true)
	ElseIf aiOption == OID_Loot
		ALifeSetLoot(false)
	ElseIf aiOption == OID_Uninstall
		ALifePrepareUninstall()
	EndIf
EndEvent

Event OnOptionMenuOpen(Int aiOption)
	If aiOption == OID_Combat
		SetMenuDialogDefaultIndex(1)
	ElseIf aiOption == OID_Talk
		SetMenuDialogDefaultIndex(2)
	ElseIf aiOption == OID_Compat
		SetMenuDialogDefaultIndex(0)
	EndIf
EndEvent

Event OnOptionMenuAccept(Int aiOption, Int aiIndex)
	If aiOption == OID_Combat
		ALifeSetOfflineCombat(aiIndex)
	ElseIf aiOption == OID_Talk
		ALifeSetTalkInitiative(aiIndex)
	ElseIf aiOption == OID_Compat
		ALifeSetCompatBudget(aiIndex - 1)
	EndIf
EndEvent

Event OnOptionSliderOpen(Int aiOption)
	Int d = DiffIndex(aiOption)
	If d >= 0
		SetSliderDialogDefaultValue(1.0)
	EndIf
EndEvent

Event OnOptionSliderAccept(Int aiOption, Float afValue)
	Int d = DiffIndex(aiOption)
	If d >= 0
		ALifeSetDiffScale(d, afValue)
	EndIf
EndEvent

Event OnOptionDefault(Int aiOption)
	Int d = DiffIndex(aiOption)
	If d >= 0
		ALifeSetDiffScale(d, 1.0)
	ElseIf aiOption == OID_Follow
		ALifeSetFollowGame(true)
	ElseIf aiOption == OID_Enabled
		ALifeSetEnabled(true)
	ElseIf aiOption == OID_News
		ALifeSetNews(true)
	ElseIf aiOption == OID_DynRel
		ALifeSetDynamicRelations(false)
	ElseIf aiOption == OID_Loot
		ALifeSetLoot(true)
	ElseIf aiOption == OID_Combat
		ALifeSetOfflineCombat(1)
	ElseIf aiOption == OID_Talk
		ALifeSetTalkInitiative(2)
	ElseIf aiOption == OID_Compat
		ALifeSetCompatBudget(-1)
	EndIf
EndEvent

Event OnOptionHighlight(Int aiOption)
	Int d = DiffIndex(aiOption)
	If d >= 0
		SetInfoText("Slider")
	ElseIf aiOption == OID_Follow
		SetInfoText("Follow")
	ElseIf aiOption == OID_Enabled
		SetInfoText("Enabled")
	ElseIf aiOption == OID_News
		SetInfoText("News")
	ElseIf aiOption == OID_DynRel
		SetInfoText("Relations")
	ElseIf aiOption == OID_Loot
		SetInfoText("Loot")
	ElseIf aiOption == OID_Combat
		SetInfoText("Combat")
	ElseIf aiOption == OID_Talk
		SetInfoText("Talk")
	ElseIf aiOption == OID_Compat
		SetInfoText("Compat")
	ElseIf aiOption == OID_Uninstall
		SetInfoText("Uninstall")
	EndIf
EndEvent
