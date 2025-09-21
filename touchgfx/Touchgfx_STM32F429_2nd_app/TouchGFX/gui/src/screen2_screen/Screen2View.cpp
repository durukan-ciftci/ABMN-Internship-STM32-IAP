#include <gui/screen2_screen/Screen2View.hpp>

Screen2View::Screen2View()
{
	textAreaName.setWideTextAction(touchgfx::WIDE_TEXT_WORDWRAP);
	textAreaSize.setWideTextAction(touchgfx::WIDE_TEXT_WORDWRAP);

}

void Screen2View::function1()
{
//
	extern uint8_t file_name[30];
//    const char* value = "NEW APP";
    Unicode::strncpy(textAreaNameBuffer, (char*)file_name, TEXTAREANAME_SIZE);
    textAreaNameBuffer[TEXTAREANAME_SIZE - 1] = '\0';
    textAreaName.invalidate();

    extern uint8_t file_size[20];
    Unicode::strncpy(textAreaSizeBuffer, (char*)file_size, TEXTAREASIZE_SIZE);
    textAreaSizeBuffer[TEXTAREASIZE_SIZE - 1] = '\0';
    textAreaSize.invalidate();

}

void Screen2View::setupScreen()
{
    Screen2ViewBase::setupScreen();
}

void Screen2View::tearDownScreen()
{
    Screen2ViewBase::tearDownScreen();
}
