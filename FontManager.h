#pragma once

#include <SDL.h>
#include <SDL_ttf.h>

#include <iostream>
#include <string>


class FontManager
{

public:

    FontManager();


    ~FontManager();


    bool Init(
        const std::string& fontPath,
        int size);

    SDL_Texture* CreateTextTexture(
        SDL_Renderer* renderer,
        const std::string& text,
        SDL_Color color);

    SDL_Texture* CreateMultilineTextTexture(
        SDL_Renderer* renderer,
        const std::string& text,
        SDL_Color color);

    SDL_Surface* CreateTextSurface(
        const std::string& text,
        SDL_Color color);

    void Close();


private:

    TTF_Font* font = nullptr;       // ×ÖÌå¶ÔÏó

   
  

};