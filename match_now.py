import requests
from bs4 import BeautifulSoup

url = "https://matchtv.ru/on-air"

headers = {
    "User-Agent": "Mozilla/,5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36"
}

response = requests.get(url, headers=headers)
if response.status_code != 200:
    print("Не удалось загрузить страницу. Код ошибки:", response.status_code)
else:
    soup = BeautifulSoup(response.text, 'html.parser')
    
    # Ищем блоки с программами (обычно время в одном элементе, название в соседнем)
    programs = soup.find_all('div', class_='b-onair-schedule__item')
    
    if programs:
        print("Текущая и ближайшие программы в эфире:")
        for prog in programs[:3]:  # Выводим первые 3 для контекста
            time = prog.find('div', class_='b-onair-schedule__time')
            title = prog.find('div', class_='b-onair-schedule__title')
            if time and title:
                print(f"{time.get_text(strip=True)} — {title.get_text(strip=True)}")
    else:
        # Альтернативный поиск по тексту (если структура изменится)
        text = soup.get_text()
        if "Прямой эфир" in text or "Все на Матч" in text:
            lines = [line.strip() for line in text.splitlines() if line.strip() and (":" in line or "Прямой эфир" in line)]
            print("Найденные упоминания о эфире:")
            for line in lines[:5]:
                print(line)
        else:
            print("Информация о текущем эфире не найдена.")

    # Дополнительно можно вывести заголовок страницы или другой индикатор
    current = soup.find('div', class_='b-onair-now__title')
    if current:
        print("\nСейчас идёт:", current.get_text(strip=True))