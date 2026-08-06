#include "FontManager.h"
#include "Utils/Logger.h"

#include <vector>
#include "Utils/Logger.h"
#include <sstream>
#include "Utils/Logger.h"


FontManager::FontManager()
{

}


FontManager::~FontManager()
{
    Close();
}



bool FontManager::Init(
    const std::string& fontPath,
    int size)
{

    if (TTF_Init() != 0)
    {
        Logger::Info()
            << "TTF_Init Failed : "
            << TTF_GetError()
            << std::endl;

        return false;
    }


    font =
        TTF_OpenFont(
            fontPath.c_str(),
            size);


    if (!font)
    {
        Logger::Info()
            << "Open Font Failed : "
            << TTF_GetError()
            << std::endl;

        return false;
    }


    Logger::Info()
        << "Font Init Success"
        << std::endl;


    return true;
}



SDL_Texture* FontManager::CreateTextTexture(
    SDL_Renderer* renderer,
    const std::string& text,
    SDL_Color color)
{

    if (!font ||
        !renderer)
    {
        return nullptr;
    }


    SDL_Surface* surface =
        TTF_RenderUTF8_Blended_Wrapped(
            font,
            text.c_str(),
            color,
            600);


    if (!surface)
    {
        Logger::Info()
            << "Create Surface Failed : "
            << TTF_GetError()
            << std::endl;

        return nullptr;
    }


    SDL_Texture* texture =
        SDL_CreateTextureFromSurface(
            renderer,
            surface);


    SDL_FreeSurface(
        surface);


    return texture;
}





SDL_Texture* FontManager::CreateMultilineTextTexture(
    SDL_Renderer* renderer,
    const std::string& text,
    SDL_Color color)
{

    if (!font ||
        !renderer)
    {
        return nullptr;
    }


    std::vector<std::string> lines;


    std::stringstream ss(text);


    std::string line;


    while (std::getline(ss, line))
    {
        lines.push_back(line);
    }



    int width = 0;

    int height = 0;


    std::vector<SDL_Surface*> surfaces;



    for (auto& lineText : lines)
    {

        SDL_Surface* surface =
            TTF_RenderUTF8_Blended(
                font,
                lineText.c_str(),
                color);


        if (!surface)
        {
            continue;
        }


        surfaces.push_back(surface);


        if (surface->w > width)
        {
            width = surface->w;
        }


        height += surface->h;
    }



    if (surfaces.empty())
    {
        return nullptr;
    }



    SDL_Surface* finalSurface =
        SDL_CreateRGBSurface(
            0,
            width,
            height,
            32,
            0x00FF0000,
            0x0000FF00,
            0x000000FF,
            0xFF000000);



    if (!finalSurface)
    {
        return nullptr;
    }



    SDL_FillRect(
        finalSurface,
        nullptr,
        SDL_MapRGBA(
            finalSurface->format,
            0,
            0,
            0,
            0));



    int y = 0;



    for (auto surface : surfaces)
    {

        SDL_Rect dst;


        dst.x = 0;

        dst.y = y;

        dst.w = surface->w;

        dst.h = surface->h;



        SDL_BlitSurface(
            surface,
            nullptr,
            finalSurface,
            &dst);



        y += surface->h;


        SDL_FreeSurface(
            surface);
    }



    SDL_Texture* texture =
        SDL_CreateTextureFromSurface(
            renderer,
            finalSurface);



    SDL_FreeSurface(
        finalSurface);



    return texture;
}





SDL_Surface* FontManager::CreateTextSurface(
    const std::string& text,
    SDL_Color color)
{

    if (!font)
    {
        return nullptr;
    }



    std::vector<SDL_Surface*> surfaces;


    std::stringstream ss(text);


    std::string line;



    int width = 0;

    int height = 0;



    while (std::getline(ss, line))
    {

        SDL_Surface* surface =
            TTF_RenderUTF8_Blended(
                font,
                line.c_str(),
                color);



        if (surface)
        {

            surfaces.push_back(surface);


            if (surface->w > width)
            {
                width = surface->w;
            }


            height += surface->h;
        }
    }



    if (surfaces.empty())
    {
        return nullptr;
    }




    SDL_Surface* finalSurface =
        SDL_CreateRGBSurface(
            0,
            width,
            height,
            32,
            0x00FF0000,
            0x0000FF00,
            0x000000FF,
            0xFF000000);



    if (!finalSurface)
    {
        return nullptr;
    }



    SDL_FillRect(
        finalSurface,
        nullptr,
        SDL_MapRGBA(
            finalSurface->format,
            0,
            0,
            0,
            0));



    int y = 0;



    for (auto surface : surfaces)
    {

        SDL_Rect dst;


        dst.x = 0;

        dst.y = y;

        dst.w = surface->w;

        dst.h = surface->h;



        SDL_BlitSurface(
            surface,
            nullptr,
            finalSurface,
            &dst);



        y += surface->h;


        SDL_FreeSurface(surface);
    }



    return finalSurface;
}





void FontManager::Close()
{

    if (font)
    {
        TTF_CloseFont(font);

        font = nullptr;
    }


    TTF_Quit();

}