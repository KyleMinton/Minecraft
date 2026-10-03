// RaylibApp.exe: starts the game, which lives in RaylibGame.dll.
extern "C" __declspec(dllimport) int RaylibAppMain();

int main() {
    return RaylibAppMain();
}
