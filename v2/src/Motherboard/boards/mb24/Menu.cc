#include "Menu.hh"
#include "InterfaceBoard.hh"
#include "Types.hh"
#include "Steppers.hh"
#include "Commands.hh"
#include "Errors.hh"
#include "Tool.hh"
#include "Host.hh"
#include <util/delay.h>
#include <stdlib.h>

#define HOST_PACKET_TIMEOUT_MS 20
#define HOST_PACKET_TIMEOUT_MICROS (1000L*HOST_PACKET_TIMEOUT_MS)

#define HOST_TOOL_RESPONSE_TIMEOUT_MS 50
#define HOST_TOOL_RESPONSE_TIMEOUT_MICROS (1000L*HOST_TOOL_RESPONSE_TIMEOUT_MS)


/// Static instances of our menus
SDMenu sdMenu;
MonitorMode monitor;
JogMode jogger;
SnakeMode snake;


/// Send a query packet to the extruder
bool queryExtruderParameter(uint8_t parameter, OutPacket& responsePacket) {

	Timeout acquire_lock_timeout;
	acquire_lock_timeout.start(HOST_TOOL_RESPONSE_TIMEOUT_MS);
	while (!tool::getLock()) {
		if (acquire_lock_timeout.hasElapsed()) {
			return false;
		}
	}
	OutPacket& out = tool::getOutPacket();
	InPacket& in = tool::getInPacket();
	out.reset();
	responsePacket.reset();

	// Fill the query packet. The first byte is the toolhead index, and the
	// second is the
	out.append8(0);
	out.append8(parameter);

	// Timeouts are handled inside the toolslice code; there's no need
	// to check for timeouts on this loop.
	tool::startTransaction();
	tool::releaseLock();
	// WHILE: bounded by tool timeout in runToolSlice
	while (!tool::isTransactionDone()) {
		tool::runToolSlice();
	}
	if (in.getErrorCode() == PacketError::PACKET_TIMEOUT) {
		return false;
	} else {
		// Copy payload back. Start from 0-- we need the response code.
		for (uint8_t i = 0; i < in.getLength(); i++) {
			responsePacket.append8(in.read8(i));
		}
	}

	// Check that the extruder was able to process the request
	if (responsePacket.read8(0) != 1) {
		return false;
	}

	return true;
}


void JogMode::reset() {
}

void JogMode::update(LiquidCrystal& lcd, bool forceRedraw) {
	static PROGMEM prog_uchar jog1[] =  "Jog away!";
	static PROGMEM prog_uchar jog2[] = "  Y+         Z+";
	static PROGMEM prog_uchar jog3[] = "X-  X+         ";
	static PROGMEM prog_uchar jog4[] = "  Y-         Z-";

	if (forceRedraw) {
		lcd.clear();
		lcd.setCursor(0,0);
		lcd.writeFromPgmspace(jog1);

		lcd.setCursor(0,1);
		lcd.writeFromPgmspace(jog2);

		lcd.setCursor(0,2);
		lcd.writeFromPgmspace(jog3);

		lcd.setCursor(0,3);
		lcd.writeFromPgmspace(jog4);
	}
}

void JogMode::jog(InterfaceBoardDefinitions::ButtonName direction) {
	Point position = steppers::getPosition();

	uint8_t steps = 50;
	int32_t interval = 2000;

	switch(direction) {
	case InterfaceBoardDefinitions::XMINUS:
		position[0] -= steps;
		break;
	case InterfaceBoardDefinitions::XPLUS:
		position[0] += steps;
		break;
	case InterfaceBoardDefinitions::YMINUS:
		position[1] -= steps;
		break;
	case InterfaceBoardDefinitions::YPLUS:
		position[1] += steps;
		break;
	case InterfaceBoardDefinitions::ZMINUS:
		position[2] -= steps;
		break;
	case InterfaceBoardDefinitions::ZPLUS:
		position[2] += steps;
		break;
	}

	steppers::setTarget(position, interval);
}

void JogMode::notifyButtonPressed(InterfaceBoardDefinitions::ButtonName button) {
	switch (button) {
	case InterfaceBoardDefinitions::YMINUS:
	case InterfaceBoardDefinitions::ZMINUS:
	case InterfaceBoardDefinitions::YPLUS:
	case InterfaceBoardDefinitions::ZPLUS:
	case InterfaceBoardDefinitions::XMINUS:
	case InterfaceBoardDefinitions::XPLUS:
		jog(button);
		break;
	case InterfaceBoardDefinitions::CANCEL:
		interfaceboard::popScreen();
		break;
	}
}


void SnakeMode::update(LiquidCrystal& lcd, bool forceRedraw) {
	static PROGMEM prog_uchar gameOver[] =  "GAME OVER!";

	// If we are dead, restart the game.
	if (!snakeAlive) {
		reset();
		forceRedraw = true;
	}

	if (forceRedraw) {
		lcd.clear();

		for (uint8_t i = 0; i < snakeLength; i++) {
			lcd.setCursor(snakeBody[i].x, snakeBody[i].y);
			lcd.write('O');
		}
	}

	// Always redraw the apple, just in case.
	lcd.setCursor(applePosition.x, applePosition.y);
	lcd.write('*');

	// First, undraw the snake's tail
	lcd.setCursor(snakeBody[snakeLength-1].x, snakeBody[snakeLength-1].y);
	lcd.write(' ');

	// Then, shift the snakes body parts back, deleting the tail
	for(int8_t i = snakeLength-1; i >= 0; i--) {
		snakeBody[i+1] = snakeBody[i];
	}

	// Create a new head for the snake (this causes it to move forward)
	switch(snakeDirection)
	{
	case DIR_EAST:
		snakeBody[0].x = (snakeBody[0].x + 1) % LCD_SCREEN_WIDTH;
		break;
	case DIR_WEST:
		snakeBody[0].x = (snakeBody[0].x +  LCD_SCREEN_WIDTH - 1) % LCD_SCREEN_WIDTH;
		break;
	case DIR_NORTH:
		snakeBody[0].y = (snakeBody[0].y + LCD_SCREEN_HEIGHT - 1) % LCD_SCREEN_HEIGHT;
		break;
	case DIR_SOUTH:
		snakeBody[0].y = (snakeBody[0].y + 1) % LCD_SCREEN_HEIGHT;
		break;
	}

	// Now, draw the snakes new head
	lcd.setCursor(snakeBody[0].x, snakeBody[0].y);
	lcd.write('O');

	// Check if the snake has run into itself
	for (uint8_t i = 1; i < snakeLength; i++) {
		if (snakeBody[i].x == snakeBody[0].x
			&& snakeBody[i].y == snakeBody[0].y) {
			snakeAlive = false;

			lcd.setCursor(1,1);
			lcd.writeFromPgmspace(gameOver);
			updateRate = 5000L * 1000L;
		}
	}

	// If the snake just ate an apple, increment count and make new apple
	if (snakeBody[0].x == applePosition.x
			&& snakeBody[0].y == applePosition.y) {
		applesEaten++;

		if(applesEaten % APPLES_BEFORE_GROW == 0) {
			snakeLength++;
			updateRate -= 5L * 1000L;
		}

		applePosition.x = rand()%LCD_SCREEN_WIDTH;
		applePosition.y = rand()%LCD_SCREEN_HEIGHT;

		lcd.setCursor(applePosition.x, applePosition.y);
		lcd.write('*');
	}
}

void SnakeMode::reset() {
	updateRate = 150L * 1000L;
	snakeDirection = DIR_EAST;
	snakeLength = 3;
	applesEaten = 0;
	snakeAlive = true;

	// Put the snake in an initial position
	snakeBody[0].x = 2; snakeBody[0].y = 1;
	snakeBody[1].x = 1; snakeBody[1].y = 1;
	snakeBody[2].x = 0; snakeBody[2].y = 1;

	// Put the apple in an initial position (this could collide with the snake!)
	applePosition.x = rand()%LCD_SCREEN_WIDTH;
	applePosition.y = rand()%LCD_SCREEN_HEIGHT;
}


void SnakeMode::notifyButtonPressed(InterfaceBoardDefinitions::ButtonName button) {
	switch (button) {
	case InterfaceBoardDefinitions::YMINUS:
		snakeDirection = DIR_SOUTH;
		break;
	case InterfaceBoardDefinitions::YPLUS:
		snakeDirection = DIR_NORTH;
		break;
	case InterfaceBoardDefinitions::XMINUS:
		snakeDirection = DIR_WEST;
		break;
	case InterfaceBoardDefinitions::XPLUS:
		snakeDirection = DIR_EAST;
		break;
	case InterfaceBoardDefinitions::CANCEL:
		interfaceboard::popScreen();
		break;
	}
}


void MonitorMode::reset() {
}

void MonitorMode::update(LiquidCrystal& lcd, bool forceRedraw) {
	static PROGMEM prog_uchar extruder_temp[] =   "Tool:    /   C";
	static PROGMEM prog_uchar platform_temp[] =   "Bed:     /   C";

	if (forceRedraw) {
		lcd.clear();
		lcd.setCursor(0,0);
		switch(host::getHostState()) {
		case host::HOST_STATE_READY:
			lcd.writeString(host::getMachineName());
			break;
		case host::HOST_STATE_BUILDING:
		case host::HOST_STATE_BUILDING_FROM_SD:
			lcd.writeString(host::getBuildName());
			break;
		case host::HOST_STATE_ERROR:
			lcd.writeString("error!");
			break;
		}

		lcd.setCursor(0,2);
		lcd.writeFromPgmspace(extruder_temp);

		lcd.setCursor(0,3);
		lcd.writeFromPgmspace(platform_temp);
	} else {
	}


	OutPacket responsePacket;

	// Redraw tool info
	if (queryExtruderParameter(SLAVE_CMD_GET_TEMP, responsePacket)) {
		lcd.setCursor(6,2);
		uint16_t data = responsePacket.read16(1);
		lcd.writeInt(data,3);
	}

	if (queryExtruderParameter(SLAVE_CMD_GET_SP, responsePacket)) {
		lcd.setCursor(10,2);
		uint16_t data = responsePacket.read16(1);
		lcd.writeInt(data,3);
	}

	if (queryExtruderParameter(SLAVE_CMD_GET_PLATFORM_TEMP, responsePacket)) {
		lcd.setCursor(6,3);
		uint16_t data = responsePacket.read16(1);
		lcd.writeInt(data,3);
	}

	if (queryExtruderParameter(SLAVE_CMD_GET_PLATFORM_SP, responsePacket)) {
		lcd.setCursor(10,3);
		uint16_t data = responsePacket.read16(1);
		lcd.writeInt(data,3);
	}
}

void MonitorMode::notifyButtonPressed(InterfaceBoardDefinitions::ButtonName button) {
	switch (button) {
	case InterfaceBoardDefinitions::CANCEL:
		switch(host::getHostState()) {
		case host::HOST_STATE_BUILDING:
		case host::HOST_STATE_BUILDING_FROM_SD:
			interfaceboard::pushScreen(&cancelBuildMenu);
			break;
		default:
			interfaceboard::popScreen();
			break;
		}
	}
}


void Menu::update(LiquidCrystal& lcd, bool forceRedraw) {
	// Do we need to redraw the whole menu?
	if ((itemIndex/LCD_SCREEN_HEIGHT) != (lastDrawIndex/LCD_SCREEN_HEIGHT)
			|| forceRedraw ) {
		// Redraw the whole menu
		lcd.clear();

		for (uint8_t i = 0; i < LCD_SCREEN_HEIGHT; i++) {
			if (i+(itemIndex/LCD_SCREEN_HEIGHT)*LCD_SCREEN_HEIGHT +1 > itemCount) {
				break;
			}

			lcd.setCursor(1,i);
			// Draw one page of items at a time
			drawItem(i+(itemIndex/LCD_SCREEN_HEIGHT)*LCD_SCREEN_HEIGHT, lcd);
		}
	}
	else {
		// Only need to clear the previous cursor
		lcd.setCursor(0,(lastDrawIndex%LCD_SCREEN_HEIGHT));
		lcd.write(' ');
	}

	lcd.setCursor(0,(itemIndex%LCD_SCREEN_HEIGHT));
	lcd.write('>');
	lastDrawIndex = itemIndex;
}

void Menu::reset() {
	firstItemIndex = 0;
	itemIndex = 0;
	lastDrawIndex = 255;

	resetState();
}

void Menu::resetState() {
}

void Menu::handleSelect(uint8_t index) {
}

void Menu::handleCancel() {
	// Remove ourselves from the menu list
	interfaceboard::popScreen();
}

void Menu::notifyButtonPressed(InterfaceBoardDefinitions::ButtonName button) {
	switch (button) {
	case InterfaceBoardDefinitions::ZERO:
	case InterfaceBoardDefinitions::OK:
		handleSelect(itemIndex);
		break;
	case InterfaceBoardDefinitions::CANCEL:
		handleCancel();
		break;
	case InterfaceBoardDefinitions::YMINUS:
	case InterfaceBoardDefinitions::ZMINUS:
		// increment index
		if (itemIndex < itemCount - 1) {
			itemIndex++;
		}
		break;
	case InterfaceBoardDefinitions::YPLUS:
	case InterfaceBoardDefinitions::ZPLUS:
		// decrement index
		if (itemIndex > firstItemIndex) {
			itemIndex--;
		}
		break;

	case InterfaceBoardDefinitions::XMINUS:
	case InterfaceBoardDefinitions::XPLUS:
		break;
	}
}


CancelBuildMenu::CancelBuildMenu() {
	itemCount = 4;
	reset();
}

void CancelBuildMenu::resetState() {
	itemIndex = 2;
	firstItemIndex = 2;
}

void CancelBuildMenu::drawItem(uint8_t index, LiquidCrystal& lcd) {
	static PROGMEM prog_uchar cancel[] = "Cancel Build?";
	static PROGMEM prog_uchar yes[] =   "Yes";
	static PROGMEM prog_uchar no[] =   "No";

	switch (index) {
	case 0:
		lcd.writeFromPgmspace(cancel);
		break;
	case 1:
		break;
	case 2:
		lcd.writeFromPgmspace(yes);
		break;
	case 3:
		lcd.writeFromPgmspace(no);
		break;
	}
}

void CancelBuildMenu::handleSelect(uint8_t index) {
	switch (index) {
	case 2:
		// Cancel build, returning to whatever menu came before monitor mode.
		// TODO: Cancel build.
		host::stopBuild();
		interfaceboard::popScreen();
		interfaceboard::popScreen();
		break;
	case 3:
		// Don't cancel, just close dialog.
		interfaceboard::popScreen();
		break;
	}
}


MainMenu::MainMenu() {
	itemCount = 5;
	reset();
}

void MainMenu::drawItem(uint8_t index, LiquidCrystal& lcd) {
	static PROGMEM prog_uchar monitor[] = "Monitor Mode";
	static PROGMEM prog_uchar build[] =   "Build from SD";
	static PROGMEM prog_uchar jog[] =   "Jog Mode";
	static PROGMEM prog_uchar snake[] =   "Snake Game";

	switch (index) {
	case 0:
		lcd.writeFromPgmspace(monitor);
		break;
	case 1:
		lcd.writeFromPgmspace(build);
		break;
	case 2:
		lcd.writeFromPgmspace(jog);
		break;
	case 3:
		// blank
		break;
	case 4:
		lcd.writeFromPgmspace(snake);
		break;
	}
}

void MainMenu::handleSelect(uint8_t index) {
	switch (index) {
		case 0:
			// Show monitor build screen
			interfaceboard::pushScreen(&monitor);
			break;
		case 1:
			// Show build from SD screen
			interfaceboard::pushScreen(&sdMenu);
			break;
		case 2:
			// Show build from SD screen
			interfaceboard::pushScreen(&jogger);
			break;
		case 4:
			// Show build from SD screen
			interfaceboard::pushScreen(&snake);
			break;
		}
}


SDMenu::SDMenu() {
	reset();
}

void SDMenu::resetState() {
	itemCount = countFiles();
}

// Count the number of files on the SD card
uint8_t SDMenu::countFiles() {
	uint8_t count = 0;

	sdcard::SdErrorCode e;

	// First, reset the directory index
	e = sdcard::directoryReset();
	if (e != sdcard::SD_SUCCESS) {
		// TODO: Report error
		return 6;
	}

	const int MAX_FILE_LEN = 2;
	char fnbuf[MAX_FILE_LEN];

	// Count the files
	do {
		e = sdcard::directoryNextEntry(fnbuf,MAX_FILE_LEN);
		if (fnbuf[0] == '\0') {
			break;
		}

		// If it's a dot file, don't count it.
		if (fnbuf[0] == '.') {
		}
		else {
			count++;
		}
	} while (e == sdcard::SD_SUCCESS);

	// TODO: Check for error again?

	return count;
}

sdcard::SdErrorCode SDMenu::getFilename(uint8_t index, char buffer[], uint8_t buffer_size) {
	sdcard::SdErrorCode e;

	// First, reset the directory list
	e = sdcard::directoryReset();
	if (e != sdcard::SD_SUCCESS) {
		return e;
	}


	for(uint8_t i = 0; i < index+1; i++) {
		// Ignore dot-files
		do {
			e = sdcard::directoryNextEntry(buffer,buffer_size);
			if (buffer[0] == '\0') {
				return e;
			}
		} while (e == sdcard::SD_SUCCESS && buffer[0] == '.');

		if (e != sdcard::SD_SUCCESS) {
			return e;
		}
	}

	return e;
}

void SDMenu::drawItem(uint8_t index, LiquidCrystal& lcd) {
	if (index > itemCount - 1) {
		// TODO: report error
		return;
	}

	const uint8_t MAX_FILE_LEN = LCD_SCREEN_WIDTH;
	char fnbuf[MAX_FILE_LEN];

	sdcard::SdErrorCode e;
	e = getFilename(index, fnbuf, MAX_FILE_LEN);

	if (e != sdcard::SD_SUCCESS) {
		// TODO: report error
		return;
	}

	uint8_t idx;
	for (idx = 0; (idx < MAX_FILE_LEN) && (fnbuf[idx] != 0); idx++) {
		lcd.write(fnbuf[idx]);
	}
}

void SDMenu::handleSelect(uint8_t index) {
	if (host::getHostState() != host::HOST_STATE_READY) {
		// TODO: report error
		return;
	}

	char* buildName = host::getBuildName();
	sdcard::SdErrorCode e;

	e = getFilename(index, buildName, host::MAX_FILE_LEN);
	if (e != sdcard::SD_SUCCESS) {
		// TODO: report error
		return;
	}

	e = host::startBuildFromSD();
	if (e != sdcard::SD_SUCCESS) {
		// TODO: report error
		return;
	}

	// Jump to the build monitor
	interfaceboard::pushScreen(&monitor);
}
