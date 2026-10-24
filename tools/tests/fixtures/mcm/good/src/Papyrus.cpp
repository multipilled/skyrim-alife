// SYNTHETIC FIXTURE for tools/tests/test_mcm_check.py: the registration lines tools/mcm_check.py parses.
bool Register(VM* vm)
{
	vm->RegisterFunction("ALifeGetEnabled"sv, kScript, GetEnabled);
	vm->RegisterFunction("ALifeSetEnabled"sv, kScript, SetEnabled);
	vm->RegisterFunction("ALifeSetNews"sv, kScript, SetNews);
	vm->RegisterFunction("ALifeSetDynamicRelations"sv, kScript, SetDynamicRelations);
	vm->RegisterFunction("ALifeSetLoot"sv, kScript, SetLoot);
	vm->RegisterFunction("ALifeSetOfflineCombat"sv, kScript, SetOfflineCombat);
	vm->RegisterFunction("ALifeSetTalkInitiative"sv, kScript, SetTalkInitiative);
	vm->RegisterFunction("ALifeSetCompatBudget"sv, kScript, SetCompatBudget);
	vm->RegisterFunction("ALifeSetFollowGame"sv, kScript, SetFollowGame);
	vm->RegisterFunction("ALifeGetDiffScale"sv, kScript, GetDiffScale);
	vm->RegisterFunction("ALifeSetDiffScale"sv, kScript, SetDiffScale);
	vm->RegisterFunction("ALifeStatus"sv, kScript, Status);
	vm->RegisterFunction("ALifePrepareUninstall"sv, kScript, PrepareUninstall);
	return true;
}
