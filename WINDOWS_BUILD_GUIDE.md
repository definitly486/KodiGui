# Windows Build Guide для KodiGui

## Требования

### Установка инструментов
1. **Qt Framework** (версия 5.15+ или 6.x)
   - Скачивайте с https://www.qt.io/download
   - Убедитесь, что установлены компоненты для MSVC или MinGW

2. **Visual Studio Build Tools** или **MinGW**
   - Visual Studio Community 2019/2022 (рекомендуется)
   - Или MinGW из Qt installer

3. **Git for Windows** (опционально, для ssh командной строки)
   - https://git-scm.com/download/win

### Установка зависимостей

#### libssh2 и OpenSSL для Windows

**Вариант 1: Скачать готовые бинарники**

1. Создайте структуру папок:
   ```
   libs/
     ├── libssh2/
     │   ├── include/
     │   └���─ lib/
     └── openssl/
         ├── include/
         └── lib/
   ```

2. Скачивайте libssh2 для Windows:
   - https://www.libssh2.org/download/
   - Или используйте vcpkg: `vcpkg install libssh2:x64-windows openssl:x64-windows`

3. Скопируйте `.h` файлы в `libs/libssh2/include`
4. Скопируйте `.lib` файлы в `libs/libssh2/lib`
5. Повторите для OpenSSL

**Вариант 2: Использовать vcpkg (рекомендуется)**

```bash
# Установка vcpkg
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat

# Установка зависимостей
.\vcpkg install libssh2:x64-windows openssl:x64-windows

# Интеграция с Qt
.\vcpkg integrate install
```

### Python для Windows

Для работы скрипта `match_now.py`:

```bash
pip install requests beautifulsoup4
```

## Сборка проекта

### 1. Откройте проект в Qt Creator

```bash
qt-creator kodigui.pro
```

### 2. Настройте пути к libssh2 (если не использовали vcpkg)

Отредактируйте `kodigui.pro` и установите правильные пути:

```qmake
win32 {
    LIBS += -LC:\path\to\libssh2\lib -lssh2
    INCLUDEPATH += C:\path\to\libssh2\include
    LIBS += -LC:\path\to\openssl\lib -lssl -lcrypto
    INCLUDEPATH += C:\path\to\openssl\include
}
```

### 3. Настройте kit в Qt Creator

- Tools → Options → Kits
- Выберите MSVC 2019/2022 x64 или MinGW
- Убедитесь, что Qt версия правильная

### 4. Откройте терминал и соберите

```bash
mkdir build
cd build
qmake ..\kodigui.pro
make  # или nmake для MSVC
```

### Или в Qt Creator

- Build → Build All
- Run (Ctrl+R)

## Совместимость Windows/Linux

Проект использует условную компиляцию `Q_OS_WIN` для разных платформ:

- **Пути к файлам**: Используется `QStandardPaths::writableLocation(QStandardPaths::TempLocation)` для кроссплатформенности
- **Socket API**: `winsock2.h` для Windows, `sys/socket.h` для Unix
- **Закрытие сокетов**: `closesocket()` для Windows, `close()` для Unix
- **SSH команды**: Одинаковые для обеих платформ (требуется ssh.exe на Windows)

## Установка SSH на Windows

Для работы SSH команд:

### Вариант 1: Git Bash (Git for Windows)
Уже включает ssh.exe

### Вариант 2: OpenSSH для Windows (Windows 10+)
```powershell
Add-WindowsCapability -Online -Name OpenSSH.Client~~~~0.0.1.0
```

### Вариант 3: PuTTY + Pageant
- Установите PuTTY из https://www.putty.org/
- Настройте SSH ключи

## Решение проблем

### Ошибка: Cannot find libssh2
```
error LNK1104: cannot open file 'ssh2.lib'
```

**Решение**: 
- Проверьте пути в `kodigui.pro`
- Убедитесь, что `.lib` файлы на месте
- Используйте полный путь: `LIBS += -LC:\..."` 

### Ошибка: winsock2.h not found
- Уже включен в Windows SDK (поставляется с Visual Studio)
- Убедитесь, что установлены Windows SDK компоненты

### SSH команды не работают
- Убедитесь, что `ssh.exe` в PATH
- Используйте полный путь: `"C:\Program Files\Git\usr\bin\ssh.exe"`
- Или добавьте в PATH переменную окружения

### Python скрипт не запускается
```
Python command not found
```

**Решение**:
- Убедитесь, что Python установлен и в PATH
- Измените `pythonCmd` в коде:
  ```cpp
  QString pythonCmd = "C:\\Users\\YourUser\\AppData\\Local\\Programs\\Python\\Python39\\python.exe";
  ```

## Создание инсталлятора

Используйте Qt Installer Framework:

```bash
cd release
windeployqt kodigui.exe
# Создайте инсталлятор с помощью Qt IFW
```

Ор используйте NSIS для создания `.exe` инсталлятора.

## Тестирование

```bash
# Запустите с отладкой
qt-creator --debug kodigui.pro

# Или из командной строки
.\build\release\kodigui.exe
```
