// SYNTHETIC FIXTURE for tools/tests/test_mcm_check.py: the ini keys the plugin writes.
void Save(std::ostream& out, const Data& g)
{
	out << "[General]\n";
	out << "Enabled=" << g.enabled << "\n";
	out << "FollowGameDifficulty=" << 1 << "\n";
	out << "OfflineCombat=" << g.ocsMode << "\n";
	out << "News=" << g.news << "\n";
	out << "DynamicRelations=" << g.dynamicRelations << "\n";
	out << "TalkInitiative=" << g.talkInitiative << "\n";
	out << "Loot=" << g.loot << "\n";
	out << "iCompatBudget=" << g.compatBudget << "\n";
	out << "bCompatHandover=" << g.compatHandover << "\n";
	out << "PrepareUninstall=" << g.prepareUninstall << "\n";
	out << "[Debug]\n";
	out << "bDebug=" << g.bDebug << "\n";
}
