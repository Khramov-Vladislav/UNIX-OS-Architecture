# flatcopy.bash

Лабораторная работа №2 (вариант 12). Сценарий на bash, который копирует все **обычные файлы** из исходной директории и всех её поддиректорий в целевую директорию **без сохранения структуры поддиректорий**. Если имена файлов повторяются, выдаётся предупреждение, а в целевой директории остаётся последний обработанный файл.

## Синтаксис

```
flatcopy.bash [-h|--help] [-v|--verbose] [-n|--nothing] [-p|--parents] <исходная_директория> <целевая_директория>
```

| Модификатор | Мнемоника | Действие |
|---|---|---|
| `-h`, `--help` | help | краткая справка и выход |
| `-v`, `--verbose` | verbose | печатать каждый копируемый файл и итог |
| `-n`, `--nothing` | nothing | имитация: показать, что будет сделано, ничего не копировать (включает `-v`) |
| `-p`, `--parents` | parents | создать целевую директорию, если её нет |

Модификаторы разбираются через `getopts` (POSIX). Длинные формы заменяются на короткие до разбора. Можно склеивать короткие (`-vn`), `--` отделяет операнды.

## Коды возврата

| Код | Значение |
|---|---|
| 0 | успешно |
| 1 | ошибки при копировании |
| 2 | неверные модификаторы или аргументы |

## Проверки

- существование и тип исходной и целевой директорий;
- права на чтение исходной и на запись целевой директории;
- исходная и целевая директории не совпадают;
- целевая директория внутри исходной пропускается при обходе;
- имена с пробелами и спецсимволами.

## Тестовое окружение

Директория `test/` содержит всё для ручной проверки. Скрипт `test/setup.sh` пересоздаёт окружение и работает только внутри `test/`:

```bash
test/setup.sh
```

```
test/
├── setup.sh            пересоздаёт окружение
├── src/                исходное дерево (4 файла a.txt, скрытый файл, имена с пробелами,
│                       пустая директория, символическая ссылка)
├── dst/                пустая целевая директория
├── dst with space/     целевая с пробелом в имени
├── ro_dst/             без права записи (555)
└── noread_src/         без права чтения (000)
```

В git хранятся `setup.sh` и `src/`. Остальное (`dst`, `ro_dst`, `noread_src`, пустые директории и права доступа) git сохранить не может, поэтому после клонирования запустите `test/setup.sh`.

Все команды ниже выполняются из директории `lab2/`. Перед каждой командой стоит свежий `test/setup.sh`, пути в выводе сокращены до `/path/to/lab2`.

## Справка

```bash
./flatcopy.bash -h
```

```
 flatcopy.bash - плоское копирование обычных файлов.

 НАЗНАЧЕНИЕ
   Копирует все обычные файлы (regular files) из исходной директории и всех
   её поддиректорий в целевую директорию. Структура поддиректорий не
   воспроизводится: все файлы оказываются непосредственно в целевой
   директории. Если встречаются файлы с одинаковыми именами, выдаётся
   предупреждение, а в целевой директории остаётся последний обработанный
   файл.

 ИСПОЛЬЗОВАНИЕ
   flatcopy.bash [-h|--help] [-v|--verbose] [-n] [-p] <исходная_директория> <целевая_директория>

 МОДИФИКАТОРЫ
   -h, --help      (help)     вывести краткую справку и завершить работу;
   -v, --verbose   (verbose)  выводить информацию о каждом копируемом файле;
   -n, --nothing   (nothing)  показать, что будет сделано, но ничего не копировать;
   -p, --parents   (parents)  создать целевую директорию, если она не существует.

 ВХОДНЫЕ АРГУМЕНТЫ
   <исходная_директория>  существующая директория, доступная для чтения;
   <целевая_директория>   существующая (или создаваемая с -p) директория,
                          доступная для записи; не должна совпадать
                          с исходной.

 ОСОБЕННОСТИ
   * Копируются только обычные файлы
   * Если целевая директория находится внутри исходной, её содержимое
     при обходе пропускается.
   * Имена файлов могут содержать пробелы, переводы строк и любые
     спецсимволы.

 КОДЫ ВОЗВРАТА
   0 - успешно;
   1 - ошибка при копировании файлов;
   2 - неверные модификаторы или аргументы.
```
# код возврата: 0

Тот же результат даёт `./flatcopy.bash --help`.

## Примеры

### Имитация (`-n`)

Ничего не копируется, показывается план. `-n` автоматически включает подробный вывод.

```
$ ./flatcopy.bash -n test/src test/dst
'/path/to/lab2/test/src/.hidden' -> '/path/to/lab2/test/dst/.hidden'
'/path/to/lab2/test/src/sub1/deep/a.txt' -> '/path/to/lab2/test/dst/a.txt'
'/path/to/lab2/test/src/sub1/b.txt' -> '/path/to/lab2/test/dst/b.txt'
flatcopy.bash: предупреждение: дубликат имени 'a.txt': '/path/to/lab2/test/src/sub1/a.txt' заменяет ранее скопированный файл
'/path/to/lab2/test/src/sub1/a.txt' -> '/path/to/lab2/test/dst/a.txt'
flatcopy.bash: предупреждение: дубликат имени 'a.txt': '/path/to/lab2/test/src/a.txt' заменяет ранее скопированный файл
'/path/to/lab2/test/src/a.txt' -> '/path/to/lab2/test/dst/a.txt'
'/path/to/lab2/test/src/sub2/c.txt' -> '/path/to/lab2/test/dst/c.txt'
flatcopy.bash: предупреждение: дубликат имени 'a.txt': '/path/to/lab2/test/src/sub2/a.txt' заменяет ранее скопированный файл
'/path/to/lab2/test/src/sub2/a.txt' -> '/path/to/lab2/test/dst/a.txt'
'/path/to/lab2/test/src/папка с пробелами/my file.txt' -> '/path/to/lab2/test/dst/my file.txt'
Итого: будет скопировано файлов: 8, дубликатов имён: 3, ошибок: 0
# код возврата: 0
```

### Реальное копирование (`-v`)

Структура поддиректорий не воспроизводится. Четыре файла `a.txt` дают три предупреждения о дубликатах, остаётся последний обработанный.

```
$ ./flatcopy.bash -v test/src test/dst
'/path/to/lab2/test/src/.hidden' -> '/path/to/lab2/test/dst/.hidden'
'/path/to/lab2/test/src/sub1/deep/a.txt' -> '/path/to/lab2/test/dst/a.txt'
'/path/to/lab2/test/src/sub1/b.txt' -> '/path/to/lab2/test/dst/b.txt'
flatcopy.bash: предупреждение: дубликат имени 'a.txt': '/path/to/lab2/test/src/sub1/a.txt' заменяет ранее скопированный файл
'/path/to/lab2/test/src/sub1/a.txt' -> '/path/to/lab2/test/dst/a.txt'
flatcopy.bash: предупреждение: дубликат имени 'a.txt': '/path/to/lab2/test/src/a.txt' заменяет ранее скопированный файл
'/path/to/lab2/test/src/a.txt' -> '/path/to/lab2/test/dst/a.txt'
'/path/to/lab2/test/src/sub2/c.txt' -> '/path/to/lab2/test/dst/c.txt'
flatcopy.bash: предупреждение: дубликат имени 'a.txt': '/path/to/lab2/test/src/sub2/a.txt' заменяет ранее скопированный файл
'/path/to/lab2/test/src/sub2/a.txt' -> '/path/to/lab2/test/dst/a.txt'
'/path/to/lab2/test/src/папка с пробелами/my file.txt' -> '/path/to/lab2/test/dst/my file.txt'
Итого: скопировано файлов: 8, дубликатов имён: 3, ошибок: 0
# код возврата: 0
```

### Длинная форма `--verbose` и целевая директория с пробелом

```
$ ./flatcopy.bash --verbose test/src test/dst with space
'/path/to/lab2/test/src/.hidden' -> '/path/to/lab2/test/dst with space/.hidden'
'/path/to/lab2/test/src/sub1/deep/a.txt' -> '/path/to/lab2/test/dst with space/a.txt'
'/path/to/lab2/test/src/sub1/b.txt' -> '/path/to/lab2/test/dst with space/b.txt'
flatcopy.bash: предупреждение: дубликат имени 'a.txt': '/path/to/lab2/test/src/sub1/a.txt' заменяет ранее скопированный файл
'/path/to/lab2/test/src/sub1/a.txt' -> '/path/to/lab2/test/dst with space/a.txt'
flatcopy.bash: предупреждение: дубликат имени 'a.txt': '/path/to/lab2/test/src/a.txt' заменяет ранее скопированный файл
'/path/to/lab2/test/src/a.txt' -> '/path/to/lab2/test/dst with space/a.txt'
'/path/to/lab2/test/src/sub2/c.txt' -> '/path/to/lab2/test/dst with space/c.txt'
flatcopy.bash: предупреждение: дубликат имени 'a.txt': '/path/to/lab2/test/src/sub2/a.txt' заменяет ранее скопированный файл
'/path/to/lab2/test/src/sub2/a.txt' -> '/path/to/lab2/test/dst with space/a.txt'
'/path/to/lab2/test/src/папка с пробелами/my file.txt' -> '/path/to/lab2/test/dst with space/my file.txt'
Итого: скопировано файлов: 8, дубликатов имён: 3, ошибок: 0
# код возврата: 0
```

### Создание целевой директории (`-p`)

Директории `test/new_dst` не существует, она создаётся.

```
$ ./flatcopy.bash -p -v test/src test/new_dst
'/path/to/lab2/test/src/.hidden' -> '/path/to/lab2/test/new_dst/.hidden'
'/path/to/lab2/test/src/sub1/deep/a.txt' -> '/path/to/lab2/test/new_dst/a.txt'
'/path/to/lab2/test/src/sub1/b.txt' -> '/path/to/lab2/test/new_dst/b.txt'
flatcopy.bash: предупреждение: дубликат имени 'a.txt': '/path/to/lab2/test/src/sub1/a.txt' заменяет ранее скопированный файл
'/path/to/lab2/test/src/sub1/a.txt' -> '/path/to/lab2/test/new_dst/a.txt'
flatcopy.bash: предупреждение: дубликат имени 'a.txt': '/path/to/lab2/test/src/a.txt' заменяет ранее скопированный файл
'/path/to/lab2/test/src/a.txt' -> '/path/to/lab2/test/new_dst/a.txt'
'/path/to/lab2/test/src/sub2/c.txt' -> '/path/to/lab2/test/new_dst/c.txt'
flatcopy.bash: предупреждение: дубликат имени 'a.txt': '/path/to/lab2/test/src/sub2/a.txt' заменяет ранее скопированный файл
'/path/to/lab2/test/src/sub2/a.txt' -> '/path/to/lab2/test/new_dst/a.txt'
'/path/to/lab2/test/src/папка с пробелами/my file.txt' -> '/path/to/lab2/test/new_dst/my file.txt'
Итого: скопировано файлов: 8, дубликатов имён: 3, ошибок: 0
# код возврата: 0
```

### Нет прав на запись в целевую директорию

```
$ ./flatcopy.bash test/src test/ro_dst
flatcopy.bash: 'test/ro_dst': нет прав на запись в директорию
Справка: flatcopy.bash -h
# код возврата: 2
```

### Нет прав на чтение исходной директории

```
$ ./flatcopy.bash test/noread_src test/dst
flatcopy.bash: 'test/noread_src': нет прав на чтение директории
Справка: flatcopy.bash -h
# код возврата: 2
```

### Исходная директория не существует

```
$ ./flatcopy.bash test/nope test/dst
flatcopy.bash: 'test/nope': исходная директория не существует
Справка: flatcopy.bash -h
# код возврата: 2
```

### Исходный путь — не директория

```
$ ./flatcopy.bash test/src/a.txt test/dst
flatcopy.bash: 'test/src/a.txt': не является директорией
Справка: flatcopy.bash -h
# код возврата: 2
```

### Исходная и целевая директории совпадают

```
$ ./flatcopy.bash test/src test/src
flatcopy.bash: исходная и целевая директории совпадают
Справка: flatcopy.bash -h
# код возврата: 2
```

### Целевая директория внутри исходной

Уже скопированное содержимое целевой директории при обходе пропускается, поэтому ложных дубликатов не возникает (`sub1/a.txt` и `sub1/deep/a.txt` в `src` тоже пропущены).

```
$ ./flatcopy.bash -v test/src test/src/sub1
'/path/to/lab2/test/src/.hidden' -> '/path/to/lab2/test/src/sub1/.hidden'
'/path/to/lab2/test/src/a.txt' -> '/path/to/lab2/test/src/sub1/a.txt'
'/path/to/lab2/test/src/sub2/c.txt' -> '/path/to/lab2/test/src/sub1/c.txt'
flatcopy.bash: предупреждение: дубликат имени 'a.txt': '/path/to/lab2/test/src/sub2/a.txt' заменяет ранее скопированный файл
'/path/to/lab2/test/src/sub2/a.txt' -> '/path/to/lab2/test/src/sub1/a.txt'
'/path/to/lab2/test/src/папка с пробелами/my file.txt' -> '/path/to/lab2/test/src/sub1/my file.txt'
Итого: скопировано файлов: 5, дубликатов имён: 1, ошибок: 0
# код возврата: 0
```

### Не хватает аргументов

```
$ ./flatcopy.bash
flatcopy.bash: не хватает аргументов (ожидается 2, получено 0)
Справка: flatcopy.bash -h
# код возврата: 2
```

### Неизвестный модификатор

```
$ ./flatcopy.bash -x test/src test/dst
flatcopy.bash: неизвестный модификатор -- 'x'
Справка: flatcopy.bash -h
# код возврата: 2
```

### Целевая директория не существует и нет `-p`

```
$ ./flatcopy.bash test/src test/nodst
flatcopy.bash: 'test/nodst': целевая директория не существует (используйте -p для создания)
Справка: flatcopy.bash -h
# код возврата: 2
```

## Очистка

```bash
chmod -R u+rwx test   # вернуть права, если хотите удалить test/ вручную
```
