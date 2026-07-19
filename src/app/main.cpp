#include <iostream>

// Тонкая точка входа; логика приложения приходит задачами 4-8.
int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: atp_app <config.json>\n";
        return 2;
    }
    return 0;
}
