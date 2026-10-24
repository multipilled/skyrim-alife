Scriptname ALife_Companions Hidden
{A-Life companions (docs/COMPANIONS.md). The plugin calls these globals so the engine's own follower functions run: the
teammate flag (sneak with the player, no crime witness, help in fights, follow through doors) and the share-items menu.}

; Make akActor a player teammate (or stop being one). Not a vanilla follower: no CurrentFollowerFaction, no follower quest.
Function Team(Actor akActor, Bool abOn) global
	If akActor
		akActor.SetPlayerTeammate(abOn, abOn)
	EndIf
EndFunction

; The vanilla follower gear menu on akActor ("Let me see your gear").
Function Gear(Actor akActor) global
	If akActor
		akActor.OpenInventory(True)
	EndIf
EndFunction
