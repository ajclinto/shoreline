/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

#include "main_window.h"

int main(int argc, char *argv[])
{
    QApplication  app(argc, argv);

    MAIN_WINDOW window(argv[0]);

    window.show();
    return app.exec();
}
