#include "UnitTest.h"
#include "LANSync/ParseSaveFilename.h"

bool TestLANSync() {
	std::string gameId;
	int slot;

	// Normal: standard game ID + slot
	EXPECT_TRUE(LANSync::ParseSaveFilename("ULUS12345_0", gameId, slot));
	EXPECT_TRUE(gameId == "ULUS12345");
	EXPECT_EQ_INT(slot, 0);

	// Normal: slot 999 (max valid)
	EXPECT_TRUE(LANSync::ParseSaveFilename("ULUS12345_999", gameId, slot));
	EXPECT_EQ_INT(slot, 999);

	// rfind('_') : finds LAST underscore for game IDs with underscores
	EXPECT_TRUE(LANSync::ParseSaveFilename("A_B_C_1", gameId, slot));
	EXPECT_TRUE(gameId == "A_B_C");
	EXPECT_EQ_INT(slot, 1);

	// Empty gameId (starts with underscore)
	EXPECT_FALSE(LANSync::ParseSaveFilename("_123", gameId, slot));

	// Empty slot (ends with underscore)
	EXPECT_FALSE(LANSync::ParseSaveFilename("GAME_", gameId, slot));

	// Non-numeric slot
	EXPECT_FALSE(LANSync::ParseSaveFilename("GAME_abc", gameId, slot));

	// Slot out of range
	EXPECT_FALSE(LANSync::ParseSaveFilename("GAME_1000", gameId, slot));

	// Negative slot
	EXPECT_FALSE(LANSync::ParseSaveFilename("GAME_-1", gameId, slot));

	// No underscore at all
	EXPECT_FALSE(LANSync::ParseSaveFilename("NoUnderscore", gameId, slot));

	// Empty string
	EXPECT_FALSE(LANSync::ParseSaveFilename("", gameId, slot));

	// Leading zeros in slot
	EXPECT_TRUE(LANSync::ParseSaveFilename("GAME_007", gameId, slot));
	EXPECT_EQ_INT(slot, 7);

	// Mixed content in slot prefix (gameId with numbers)
	EXPECT_TRUE(LANSync::ParseSaveFilename("ULUS12345_42", gameId, slot));
	EXPECT_TRUE(gameId == "ULUS12345");
	EXPECT_EQ_INT(slot, 42);

	return true;
}
