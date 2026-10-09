Scriptname ALife_Tasks Hidden
{A-Life jobs (docs/TASKS.md). The plugin calls these globals to drive the ALifeTasks quest: the engine's own objective
functions update the journal, the HUD notice and the compass / map targets together. Objective 10*s is slot s's work,
10*s+1 its return to the giver; alias s is the work marker, alias 4+s the giver marker.}

; Start the quest if it is not running yet and keep it the tracked one.
Function Begin(Quest akQuest) global
	If akQuest && !akQuest.IsRunning()
		akQuest.Start()
	EndIf
EndFunction

; Aim alias aiAlias at akMarker and show objective aiIndex (again: a reused slot is reset first).
Function Show(Quest akQuest, Int aiIndex, Int aiAlias, ObjectReference akMarker) global
	If !akQuest
		Return
	EndIf
	If !akQuest.IsRunning()
		akQuest.Start()
	EndIf
	ReferenceAlias target = akQuest.GetAlias(aiAlias) as ReferenceAlias
	If target && akMarker
		target.ForceRefTo(akMarker)
	EndIf
	akQuest.SetObjectiveCompleted(aiIndex, False)
	akQuest.SetObjectiveFailed(aiIndex, False)
	akQuest.SetObjectiveDisplayed(aiIndex, True, True)
	akQuest.SetActive(True)
EndFunction

; aiHow: 0 hide, 2 completed, 3 failed.
Function Mark(Quest akQuest, Int aiIndex, Int aiHow) global
	If !akQuest
		Return
	EndIf
	If aiHow == 2
		akQuest.SetObjectiveCompleted(aiIndex, True)
	ElseIf aiHow == 3
		akQuest.SetObjectiveFailed(aiIndex, True)
	Else
		akQuest.SetObjectiveDisplayed(aiIndex, False)
	EndIf
EndFunction
