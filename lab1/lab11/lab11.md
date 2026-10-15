# Лабораторная работа 11. Подсистема печати

> **Примечание к оформлению.** Выводы команд ниже — типовые для Debian 13. Выполните команды на своей ВМ, сделайте
> скриншоты и замените ими заглушки `fig/...`. Номера заданий, даты и имена файлов в ваших выводах будут другими.

## Упражнение 11.1. Средства печати UNIX

> **CUPS** (Common UNIX Printing System) — стандартная подсистема печати в современных Linux/UNIX. Состав:
>
> * **`cupsd`** — демон печати: принимает задания, ставит их в очереди (спул `/var/spool/cups`), управляет принтерами;
> * **протокол IPP** (Internet Printing Protocol) — по нему клиенты общаются с демоном, порт **631**; на этом же порту
>   работает веб-интерфейс администрирования;
> * **фильтры** — преобразуют формат документа в формат, понятный принтеру (текст → PDF/PostScript → растр);
> * **бэкенды** — отправляют готовые данные «в железо» (USB, сеть, файл);
> * **PPD-файлы / драйверы** — описывают возможности конкретной модели принтера.
>
> Пользовательские команды существуют в двух семействах: BSD-стиль (`lpr`, `lpq`, `lprm`) и System V (`lp`, `lpstat`,
> `cancel`). Обе работают с одной и той же очередью.

### 1. Установите систему печати cups

```bash
vlad@debian13:~$ sudo apt update
vlad@debian13:~$ sudo apt install cups
```

Проверка, что демон запущен и включён:

```bash
vlad@debian13:~$ systemctl status cups | head -4
● cups.service - CUPS Scheduler
     Loaded: loaded (/usr/lib/systemd/system/cups.service; enabled; preset: enabled)
     Active: active (running) since Tue 2026-10-13 18:02:41 EEST; 12s ago
vlad@debian13:~$ ss -tlnp | grep 631
LISTEN 0  128  127.0.0.1:631  0.0.0.0:*
LISTEN 0  128      [::1]:631     [::]:*
```

![install cups](fig/11-01%20install%20cups.png)

Демон слушает порт 631 только на `127.0.0.1` (петлевой интерфейс), поэтому веб-интерфейс по умолчанию доступен только
с самой машины.

Для добавления принтеров нужны права администратора печати — состоять в группе `lpadmin` (иначе веб-интерфейс
запросит логин и пароль root/sudo-пользователя):

```bash
vlad@debian13:~$ sudo usermod -aG lpadmin vlad
vlad@debian13:~$ id vlad
uid=1000(vlad) gid=1000(vlad) группы=1000(vlad),...,7(lp),...,115(lpadmin)
```

Изменение группы вступает в силу после повторного входа в систему (лаб. 1, упр. 1.7).

### 2. Установите виртуальный драйвер для печати в PDF

```bash
vlad@debian13:~$ sudo apt install printer-driver-cups-pdf
```

![install cups-pdf](fig/11-02%20install%20cups-pdf.png)

Пакет `printer-driver-cups-pdf` добавляет в CUPS **виртуальный принтер**: вместо реального устройства «бумагой»
служит PDF-файл. Фильтр преобразует документ в PDF, а бэкенд `cups-pdf` кладёт его в каталог пользователя.
Каталог для готовых файлов задан в `/etc/cups/cups-pdf.conf`:

```bash
vlad@debian13:~$ grep -E '^(Out|AnonDirName|Spool)' /etc/cups/cups-pdf.conf
Out ${HOME}/PDF
AnonDirName /var/spool/cups-pdf/ANONYMOUS
Spool /var/spool/cups-pdf/SPOOL
```

Итак, PDF-файлы появятся в `~/PDF` (если `Out` отличается в вашей системе — используйте свой путь).

Проверка, какие драйверы доступны (понадобится для CLI-варианта в следующем пункте):

```bash
vlad@debian13:~$ lpinfo -m | grep -i pdf
lsb/usr/cups-pdf/CUPS-PDF_opt.ppd Generic CUPS-PDF Printer (w/ options)
lsb/usr/cups-pdf/CUPS-PDF_noopt.ppd Generic CUPS-PDF Printer
vlad@debian13:~$ sudo lpinfo -v | grep -i pdf
direct cups-pdf:/
```

- `lpinfo -m` — список доступных драйверов (моделей, PPD);
- `lpinfo -v` — список доступных устройств (бэкендов). `cups-pdf:/` — наш виртуальный принтер.

### 3. Добавьте принтер LocalPrinter через веб-интерфейс

В браузере графической среды (лаб. 10) открываем **http://127.0.0.1:631**:

1. **Administration (Администрирование)** → **Add Printer (Добавить принтер)**. При запросе вводим логин и пароль
   пользователя из группы `lpadmin` (или `sudo`).
2. В разделе *Local Printers* выбираем **CUPS-PDF (Virtual PDF Printer)** → **Continue**.
3. Заполняем параметры: **Name:** `LocalPrinter`; **Description:** `Local PDF printer`; **Location:** `localhost`; (не
   ставим «Share This Printer») → **Continue**.
4. Make (производитель): **Generic**; Model: **Generic CUPS-PDF Printer** → **Add Printer**.
5. На странице «Set Printer Options» оставляем значения по умолчанию → **Set Default Options**.

![cups web](fig/11-03%20cups%20web.png)

![add printer](fig/11-03%20add%20printer.png)

Принтер появился во вкладке **Printers**:

![printers](fig/11-03%20printers.png)

> **Если работа идёт без графической среды** (например, по ssh из macOS), есть два способа:
>
> a) пробросить порт и открыть интерфейс браузером на хозяйской машине:
> ```bash
> user@mac ~ % ssh -L 6310:127.0.0.1:631 vlad@<адрес_ВМ>
> ```
> затем в браузере macOS открыть `http://127.0.0.1:6310`;
>
> б) добавить принтер из командной строки — результат тот же:
> ```bash
> vlad@debian13:~$ sudo lpadmin -p LocalPrinter -E -v cups-pdf:/ \
>     -m lsb/usr/cups-pdf/CUPS-PDF_opt.ppd -D "Local PDF printer" -L localhost
> ```
> * `-p` — имя принтера; `-E` — включить принтер и принимать задания; `-v` — URI устройства (бэкенда);
> * `-m` — драйвер из вывода `lpinfo -m`; `-D`, `-L` — описание и расположение.

Проверка:

```bash
vlad@debian13:~$ lpstat -p -d
принтер LocalPrinter запущен.  включен с Tue 13 Oct 2026 18:20:05
принтер PDF запущен.  включен с Tue 13 Oct 2026 18:02:50
нет назначения системы по умолчанию
```

(Принтер `PDF` — виртуальный принтер, который `cups-pdf` создаёт при установке сам; задание в нём не нужно.)

Сделаем `LocalPrinter` принтером по умолчанию, чтобы не указывать его в каждой команде:

```bash
vlad@debian13:~$ sudo lpadmin -d LocalPrinter
vlad@debian13:~$ lpstat -d
назначение системы по умолчанию: LocalPrinter
```

### 4. Команды lpr, lpq, lprm (lp, lpstat, cancel)

| BSD-стиль | System V | Назначение                          |
|:----------|:---------|:------------------------------------|
| `lpr`     | `lp`     | отправить файл на печать            |
| `lpq`     | `lpstat` | показать состояние принтера/очереди |
| `lprm`    | `cancel` | удалить задание из очереди          |

Ключ `-P имя` (у `lp` — `-d имя`) выбирает принтер.

#### a. Состояние принтера LocalPrinter

```bash
vlad@debian13:~$ lpstat -p LocalPrinter
принтер LocalPrinter запущен.  включен с Tue 13 Oct 2026 18:20:05
vlad@debian13:~$ lpq -P LocalPrinter
LocalPrinter готов
заданий нет
vlad@debian13:~$ lpstat -t
назначение системы по умолчанию: LocalPrinter
устройство для LocalPrinter: cups-pdf:/
LocalPrinter принимает запросы с Tue 13 Oct 2026 18:20:05
принтер LocalPrinter запущен.  включен с Tue 13 Oct 2026 18:20:05
```

![lpstat](fig/11-04a%20lpstat.png)

- «запущен / включён» (`idle`/`enabled`) — принтер готов к работе;
- «принимает запросы» (`accepting`) — очередь принимает новые задания. Это два разных свойства: `cupsenable`/
  `cupsdisable` включают/отключают **обработку**, а `cupsaccept`/`cupsreject` — **приём** заданий в очередь;
- `заданий нет` / `no entries` — очередь пуста.

#### b. Печать файла на LocalPrinter

Печатаем текстовый файл:

```bash
vlad@debian13:~$ lpr -P LocalPrinter /etc/hostname
vlad@debian13:~$ echo "Hello from lab 11" > ~/print_test.txt
vlad@debian13:~$ lp -d LocalPrinter ~/print_test.txt
request id is LocalPrinter-2 (1 file(s))
```

![lpr](fig/11-04b%20lpr.png)

- `lpr` при успехе **ничего не выводит** (принцип «молчание — признак успеха»);
- `lp` сообщает идентификатор задания `LocalPrinter-2` (имя принтера + порядковый номер) — он нужен для `cancel`;
- в очередь файл попадает в виде копии (спул); обработку выполняют фильтры CUPS: текст → PDF.

Результат — готовый PDF-файл:

```bash
vlad@debian13:~$ ls -l ~/PDF
итого 16
-rw------- 1 vlad vlad 8456 окт 13 18:25 print_test.pdf
-rw------- 1 vlad vlad 8398 окт 13 18:24 hostname.pdf
vlad@debian13:~$ file ~/PDF/print_test.pdf
/home/vlad/PDF/print_test.pdf: PDF document, version 1.4, 1 pages
```

Имя PDF строится из названия документа (иногда с префиксом `job_N-...`).

#### c. Состояние принтера после печати

Задание обрабатывается мгновенно (PDF формируется за доли секунды), поэтому очередь пуста, но история заданий
сохраняется:

```bash
vlad@debian13:~$ lpq -P LocalPrinter
LocalPrinter готов
заданий нет
vlad@debian13:~$ lpstat -W completed -o LocalPrinter
LocalPrinter-1          vlad              1024   Tue 13 Oct 2026 18:24:11
LocalPrinter-2          vlad              1024   Tue 13 Oct 2026 18:25:03
```

![lpq](fig/11-04c%20lpq.png)

`-W completed` показывает уже **выполненные** задания.

#### d. Удаление задания из очереди

Чтобы задание успело задержаться в очереди, временно **отключаем обработку** принтера (требуются права
администратора печати):

```bash
vlad@debian13:~$ sudo cupsdisable LocalPrinter
vlad@debian13:~$ lpstat -p LocalPrinter
принтер LocalPrinter отключён с Tue 13 Oct 2026 18:30:12 -
	Paused
vlad@debian13:~$ lpr -P LocalPrinter /etc/services
vlad@debian13:~$ lpq -P LocalPrinter
LocalPrinter отключён
Rank    Owner   Job     File(s)                         Total Size
1st     vlad    3       services                        12288 bytes
```

Задание № 3 стоит в очереди и ждёт включения принтера. Удаляем его:

```bash
vlad@debian13:~$ lprm -P LocalPrinter 3
vlad@debian13:~$ lpq -P LocalPrinter
LocalPrinter отключён
заданий нет
```

То же самое командой System V:

```bash
vlad@debian13:~$ lpr -P LocalPrinter /etc/services
vlad@debian13:~$ lpstat -o LocalPrinter
LocalPrinter-4          vlad              12288   Tue 13 Oct 2026 18:32:40
vlad@debian13:~$ cancel LocalPrinter-4
vlad@debian13:~$ lpstat -o LocalPrinter
vlad@debian13:~$
```

![lprm](fig/11-04d%20lprm.png)

- `lprm -P принтер N` — удалить задание с номером `N`; `lprm -` (или `cancel -a`) удаляет **все** свои задания;
  обычный пользователь может удалять только собственные задания, администратор — любые;
- пустой вывод `lpq` / `lpstat -o` подтверждает, что очередь пуста; PDF-файл для удалённого задания не создаётся.

Включаем принтер обратно:

```bash
vlad@debian13:~$ sudo cupsenable LocalPrinter
vlad@debian13:~$ lpstat -p LocalPrinter
принтер LocalPrinter запущен.  включен с Tue 13 Oct 2026 18:34:02
```

#### e. Печать страницы руководства man на LocalPrinter

Вариант 1 — через конвейер (лаб. 6), форматированный PostScript, который выдаёт `man -t` (используется `groff`):

```bash
vlad@debian13:~$ man -t ls | lpr -P LocalPrinter
vlad@debian13:~$ ls -l ~/PDF
итого 56
-rw------- 1 vlad vlad  8456 окт 13 18:25 print_test.pdf
-rw------- 1 vlad vlad 41230 окт 13 18:40 stdin.pdf
```

Вариант 2 — если `groff` не установлен (`man -t` выдаёт ошибку), печатаем обычный текст, убрав управляющие символы
выделения утилитой `col`:

```bash
vlad@debian13:~$ man ls | col -b | lpr -P LocalPrinter
```

![man print](fig/11-04e%20man.png)

- `man -t` — вывести страницу в формате PostScript (форматирование как в печатной документации: шрифты, отступы);
- в конвейере `lpr` без имени файла читает **stdin**, поэтому заданию присваивается имя `stdin` и PDF называется
  `stdin.pdf` (при повторной печати в каталоге могут появиться файлы с номерами заданий);
- `col -b` удаляет «жирный шрифт» и подчёркивание, закодированные в выводе `man` символами `Backspace`.

Проверка содержимого получившегося PDF (на ВМ без графики — в текстовом виде):

```bash
vlad@debian13:~$ sudo apt install poppler-utils
vlad@debian13:~$ pdftotext ~/PDF/stdin.pdf - | head -5
LS(1)                            User Commands                           LS(1)

NAME
       ls - list directory contents
```

Если графическая среда доступна, PDF можно открыть просмотрщиком: `xdg-open ~/PDF/stdin.pdf`.

### Итоговая схема прохождения задания

```text
 документ (текст, PostScript)
        │  lpr / lp  (IPP, порт 631)
        ▼
   cupsd: очередь /var/spool/cups   ←  lpq / lpstat (просмотр), lprm / cancel (удаление)
        │  фильтры (text → PDF)
        ▼
   backend cups-pdf  →  ~/PDF/*.pdf
```
