

#include <cstdlib>
#include <cstring>
#include <string>
#include <cmath>
#include <sstream>
#include <fstream>
#include <iostream>

#include <thread>
#include <atomic>
#include <mutex>

#include <SFML/Window.hpp>
#include <SFML/Graphics.hpp>

#ifdef SFML_SYSTEM_WINDOWS
#define NOMINMAX
#include <Windows.h>
#include <signal.h>
#else
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <signal.h>
#endif

using namespace std;
using namespace sf;

#ifdef TERMINAL_MAIN
#include "vterm.h"
#include "vterm_keycodes.h"
#else
#include "vterm/vterm.h"
#include "vterm/vterm_keycodes.h"
#endif

#include "OptionFile.hpp"
#include "Terminal.hpp"
#include "SystemFrontend.hpp"
#ifdef SFML_SYSTEM_WINDOWS
#include "WslFrontend.hpp"
#endif

OptionFile option;
Terminal* term;

RenderWindow* win;


Sprite coverAutoscale(Texture& texture, Vector2f asize) {
	Sprite sp(texture);
	Vector2f tsize(texture.getSize());
	if (tsize.x / tsize.y > asize.x / asize.y) {
		float scale = asize.y / tsize.y;
		sp.setScale(scale, scale);
		sp.setPosition(-(tsize.x * scale - asize.x) / 2.0f, 0);
	} else {
		float scale = asize.x / tsize.x;
		sp.setScale(scale, scale);
		sp.setPosition(0, -(tsize.y * scale - asize.y) / 2.0f);
	}
	return sp;
}


int main(int argc, char* argv[]) {
    bool load = true;
	if (!option.loadFromFile("./Terminal.ini"))
        if (!option.loadFromFile("../Terminal.ini"))
            load = option.loadFromFile("../../Terminal.ini");
    
    std::cout << load << std::endl;
    
	int rows, cols;
	Vector2i cellSize = Vector2i(atoi(option.get("cell_width").c_str()), atoi(option.get("cell_height").c_str()));
	int charSize = atoi(option.get("fontsize").c_str());
	bool useBold = option.get("use_bold") == "true";
	VideoMode mode;
	bool fullscreen = (argc > 1 && strcmp(argv[1], "--fullscreen") == 0);
	if (fullscreen) {
		mode = VideoMode::getDesktopMode();
		cols = mode.width / cellSize.x;
		rows = mode.height / cellSize.y;
		const string& rc = option.get("run_on_startup");
		if (!rc.empty())
			system(rc.c_str());
	} else {
		rows = atoi(option.get("rows").c_str());
		cols = atoi(option.get("cols").c_str());
        std::cout << rows << 'x' << cols << std::endl;
		mode = VideoMode(cols * cellSize.x, rows * cellSize.y);
	}
	string bgFilename = option.get("background_image");
	Uint8 bgDarkness = atoi(option.get("bg_darkness").c_str());
	Texture bgTexture;
	Sprite bgSprite;
	bool useVbo = VertexBuffer::isAvailable();
	int scrollMaxLines = atoi(option.get("scrollback_max_lines").c_str());
	bool useWslExe = option.get("use_wsl_exe") == "true";
	int cfgUpdateRate = atoi(option.get("update_rate").c_str());
	int updateRate = cfgUpdateRate;

	if (!bgFilename.empty()) {
		if (!bgTexture.loadFromFile(bgFilename))
			bgFilename.clear();
		else {
			bgTexture.setSmooth(true);
		}
	}
	if (bgFilename.empty())
		bgDarkness = 255;
	else
		bgSprite = coverAutoscale(bgTexture, Vector2f(mode.width, mode.height));

	Font font;
#ifdef SFML_SYSTEM_WINDOWS
	//font.loadFromFile("C:\\Windows\\Fonts\\ConsolasDengXianSemiBold.ttf");
	font.loadFromFile("C:\\Windows\\Fonts\\" + option.get("font"));
#else
	//font.loadFromFile("/mnt/c/Windows/Fonts/ConsolasDengXianSemiBold.ttf");
	if (!font.loadFromFile("/usr/share/fonts/" + option.get("font")))
		if (!font.loadFromFile("/mnt/Windows/Windows/Fonts/ConsolasDengXianSemiBold.ttf"))
			if (!font.loadFromFile("/mnt/c/Windows/Fonts/ConsolasDengXianSemiBold.ttf"))
				if (!font.loadFromFile("/usr/share/fonts/truetype/unifont/unifont.ttf"))
                    font.loadFromFile("/System/Library/Fonts/Supplemental/Courier New.ttf");
#endif

	if (fullscreen)
		win = new RenderWindow(mode, "Terminal", Style::None);
	else
		win = new RenderWindow(mode, "Terminal", Style::Titlebar | Style::Close | Style::Resize);
	win->clear();
	win->display();

	if (fullscreen) {
		win->setPosition(Vector2i(0, 0));
		win->setMouseCursorVisible(false);
	}
	win->setKeyRepeatEnabled(true);

	if (!updateRate)
		win->setVerticalSyncEnabled(true);
	else
		win->setFramerateLimit(updateRate);

	VertexBuffer buf(PrimitiveType::Triangles, VertexBuffer::Dynamic);
	if (useVbo)
		buf.create((size_t)2 * rows * cols * 4);
	VertexArray arrtop;
	arrtop.setPrimitiveType(PrimitiveType::Triangles);
	vector<Vertex> arr;

#ifdef SFML_SYSTEM_WINDOWS
	bool useWsl = option.get("use_wsl_frontend") == "true";
	if (useWsl)
		term = new Terminal(new WslFrontend(option.get("wsl_backend_file"), option.get("shell"), option.get("wsl_working_dir"), rows, cols, useWslExe),
			rows, cols, cellSize, charSize, useBold, scrollMaxLines);
	else
		term = new Terminal(new SystemFrontend(option.get("shell"), rows, cols), rows, cols, cellSize, charSize, useBold, scrollMaxLines);
#else
	term = new Terminal(new SystemFrontend(option.get("shell"), rows, cols), rows, cols, cellSize, charSize, useBold, scrollMaxLines);
#endif
	term->cbSetWindowSize = [&](int width, int height) {
        std::cout << win->getSize().x << 'x' << win->getSize().y << " -> " << width << 'x' << height << std::endl;
		win->setSize(Vector2u(width, height));
		win->setView(View(FloatRect(0, 0, width, height)));
	};
	term->cbSetWindowTitle = [&](const string& title) {
		win->setTitle(String::fromUtf8(title.begin(), title.end()));
	};

	term->invalidate();
    
    win->setActive(true);

	Clock cl;
	while (win->isOpen()) {

		cl.restart();

		term->update();

		bool redrawn;
		if ((redrawn = term->redrawIfRequired(font, arr, bgDarkness == 255 ? Color::Black : Color(0, 0, 0, bgDarkness))) || fullscreen || !updateRate) {
			win->clear();

			if (bgDarkness != 255)
				win->draw(bgSprite);

			if (useVbo) {
				if (redrawn)
					buf.update(arr.data(), arr.size(), 0);
				win->draw(buf, 0, arr.size(), &font.getTexture(charSize));
			} else
				win->draw(arr.data(), arr.size(), PrimitiveType::Triangles, &font.getTexture(charSize));

			if (fullscreen) {
				arrtop.clear();
				const Vector2f pos(Mouse::getPosition(*win));
				const float sqrt2 = 1.414213562f;
				const Vector2f off1(0, 16), off2(8 * sqrt2, 8 * sqrt2);
				arrtop.append(Vertex(pos, Color::White));
				arrtop.append(Vertex(pos + off1, Color::White));
				arrtop.append(Vertex(pos + Vector2f(8, 8), Color::White));
				arrtop.append(Vertex(pos, Color::White));
				arrtop.append(Vertex(pos + off2, Color::White));
				arrtop.append(Vertex(pos + Vector2f(0, 8 * sqrt2), Color::White));
				win->draw(arrtop);
			}

			win->display();

		}

		Event e;
		e.type = Event::Count;
		while (win->pollEvent(e)) {
			if (e.type == Event::Closed)
				win->close();
			else if (e.type == Event::Resized) {
				win->setView(View(FloatRect(0, 0, e.size.width, e.size.height)));

				if (e.size.width / cellSize.x != cols || e.size.height / cellSize.y != rows) {
					cols = e.size.width / cellSize.x;
					rows = e.size.height / cellSize.y;
					buf.create((size_t)2 * rows * cols * 4);
				}

				bgSprite = coverAutoscale(bgTexture, Vector2f(e.size.width, e.size.height));
			} /*else if (e.type == Event::GainedFocus)
				updateRate = cfgUpdateRate;
			else if (e.type == Event::LostFocus)
				updateRate = 10;*/
			term->processEvent(*win, e);
		}

		if (!term->isRunning())
			win->close();

		if (!(redrawn || fullscreen) && updateRate)
			sleep(max(microseconds(0), seconds(1.0f / updateRate) - cl.getElapsedTime()));
	}

	delete win;
	delete term;

	return 0;
}

