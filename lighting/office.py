from datetime import datetime
from subprocess import run, CalledProcessError
from time import sleep
from typing import List, Dict, Callable

from PyDMXControl import Colors
from PyDMXControl.controllers import OpenDMXController as Controller
from PyDMXControl.effects.Color import Color_Chase

# Create our controller
dmx = Controller(dynamic_frame=True, suppress_ticker_behind_warnings=True)

# Load some fixtures from JSON
dmx.json.load_config('office.json')

# Define some custom colors, a global fade time and the divoom device
stairville_blue = [0, 16, 255, 0]
stairville_snow = [32, 48, 255, 0]
stairville_cyan = [0, 128, 255, 0]

fungen_blue = [0, 160, 255, 0]
fungen_cyan = [0, 255, 64, 0]

flood_warm = [255, int(255 * 0.9), int(255 * 0.5), 255]
flood_white = [int(255 * 0.9), 255, 255, 255]

desk_warm = [64, 0, 0, 255]
desk_white = [int(255 * 0.25), int(255 * 0.25), int(255 * 0.25), int(255 * 0.75)]

key_white = [int(255 * 0.75), int(255 * 0.9 * 0.75), int(255 * 0.8 * 0.75), 255]

fade_time = 5000
divoom_address = '11:75:58:2D:A8:65'


# Create all the custom state methods

# XMAS state, used throughout the day in December
# Warm shelves + flood, with snowy art/books/board, standard white desk + key
def xmas():
    dmx.clear_all_effects()

    # Fill the room with light
    for f in dmx.get_fixtures_by_name_include('Flood'):
        f.color(flood_warm, fade_time)
        f.dim(255, fade_time)

    for f in dmx.get_fixtures_by_name_include('Key'):
        f.color(key_white, fade_time)
        f.dim(int(255 * 0.25), fade_time)

    for f in dmx.get_fixtures_by_name_include('Desk'):
        f.color(desk_white, fade_time)
        f.dim(255, fade_time)

    # Have the shelves be statically lit
    for f in dmx.get_fixtures_by_name_include('Shelf'):
        f.color(Colors.Warm, fade_time)
        f.dim(64, fade_time)

    # Animate the remaining lighting
    snow_group = dmx.get_fixtures_by_name_include('Art') \
                 + dmx.get_fixtures_by_name_include('Board') \
                 + dmx.get_fixtures_by_name_include('Books')
    Color_Chase.group_apply(snow_group, 60 * 1000,
                            colors=([stairville_blue] * (len(snow_group) - 1)) + [stairville_snow])
    for f in snow_group:
        f.dim(255, fade_time)


# Nighttime state, everything off
def night():
    dmx.clear_all_effects()

    for f in dmx.get_all_fixtures():
        f.color(Colors.Black, fade_time)
        f.dim(0, fade_time)

    divoom_off()


# Morning state, used during the day outside December
# White flood + desk + key, no art/shelves/board, blue books
def day():
    dmx.clear_all_effects()

    # Fill the room with light
    for f in dmx.get_fixtures_by_name_include('Flood'):
        f.color(flood_white, fade_time)
        f.dim(int(255 * 0.5), fade_time)

    for f in dmx.get_fixtures_by_name_include('Key'):
        f.color(key_white, fade_time)
        f.dim(int(255 * 0.25), fade_time)

    for f in dmx.get_fixtures_by_name_include('Desk'):
        f.color(desk_white, fade_time)
        f.dim(255, fade_time)

    # Give the books their light
    books = dmx.get_fixtures_by_name_include('Books')
    Color_Chase.group_apply(books, 60 * 1000,
                            colors=([stairville_blue] * (len(books) - 1)) + [stairville_cyan])
    for f in books:
        f.dim(255, fade_time)

    # Don't light the shelving etc. for now
    off_group = dmx.get_fixtures_by_name_include('Art') \
                + dmx.get_fixtures_by_name_include('Board') \
                + dmx.get_fixtures_by_name_include('Shelf')
    for f in off_group:
        f.color(Colors.Black, fade_time)
        f.dim(0, fade_time)

    divoom_on()


# Afternoon state, used later in the day outside December
# Warm flood, standard white desk + key, blue art/books/board/shelves
def full():
    dmx.clear_all_effects()

    # Fill the room with light
    for f in dmx.get_fixtures_by_name_include('Flood'):
        f.color(flood_warm, fade_time)
        f.dim(255, fade_time)

    for f in dmx.get_fixtures_by_name_include('Key'):
        f.color(key_white, fade_time)
        f.dim(int(255 * 0.25), fade_time)

    for f in dmx.get_fixtures_by_name_include('Desk'):
        f.color(desk_white, fade_time)
        f.dim(255, fade_time)

    # Animate the shelving lighting
    shelves = dmx.get_fixtures_by_name_include('Shelf')
    Color_Chase.group_apply(shelves, 60 * 1000, colors=([fungen_blue] * (len(shelves) - 1)) + [fungen_cyan])
    for f in shelves:
        f.dim(255, fade_time)

    # Animate the remaining lighting
    main_group = dmx.get_fixtures_by_name_include('Art') \
                 + dmx.get_fixtures_by_name_include('Board') \
                 + dmx.get_fixtures_by_name_include('Books')
    Color_Chase.group_apply(main_group, 60 * 1000,
                            colors=([stairville_blue] * (len(main_group) - 1)) + [stairville_cyan])
    for f in main_group:
        f.dim(255, fade_time)


# Late at night state
# No flood + key, warm desk + art/board/shelves, blue books
def late():
    dmx.clear_all_effects()

    # Darkness
    for f in dmx.get_fixtures_by_name_include('Flood'):
        f.color(Colors.Black, fade_time)
        f.dim(0, fade_time)

    for f in dmx.get_fixtures_by_name_include('Key'):
        f.color(Colors.Black, fade_time)
        f.dim(0, fade_time)

    # Set the desk to be warm
    for f in dmx.get_fixtures_by_name_include('Desk'):
        f.color(desk_warm, fade_time)
        f.dim(int(255 * 0.5), fade_time)

    # Give the books their light
    books = dmx.get_fixtures_by_name_include('Books')
    Color_Chase.group_apply(books, 60 * 1000,
                            colors=([stairville_blue] * (len(books) - 1)) + [stairville_cyan])
    for f in books:
        f.dim(255, fade_time)

    # Set everything else to be warm
    dim_group = dmx.get_fixtures_by_name_include('Art') \
                + dmx.get_fixtures_by_name_include('Board') \
                + dmx.get_fixtures_by_name_include('Shelf')
    for f in dim_group:
        f.color(Colors.Warm, fade_time)
        f.dim(int(255 * 0.5), fade_time)


def divoom_off():
    try:
        run(['divoom-control', 'set-brightness', '-a', divoom_address, '-b', '0'], shell=True)
        sleep(2)
        run(['divoom-control', 'set-brightness', '-a', divoom_address, '-b', '0'], shell=True)
    except CalledProcessError as e:
        print('Divoom control error:\nExit code: ', e.returncode, '\nOutput: ', e.stderr.decode('utf-8'))


def divoom_on():
    try:
        run(['divoom-control', 'set-brightness', '-a', divoom_address, '-b', '100'], shell=True)
        sleep(2)
        run(['divoom-control', 'display-custom', '-a', divoom_address], shell=True)
        sleep(2)
        run(['divoom-control', 'set-brightness', '-a', divoom_address, '-b', '100'], shell=True)
        sleep(2)
        run(['divoom-control', 'display-custom', '-a', divoom_address], shell=True)
    except CalledProcessError as e:
        print('Divoom control error:\nExit code: ', e.returncode, '\nOutput: ', e.stderr.decode('utf-8'))


# Create a time map of states for each day
def get_times() -> List[Dict[int, Callable]]:
    times = [
        {0: night, 1200: day, 1300: full, 2200: late, 2300: night},  # Monday
        {0: night, 1200: day, 1300: full, 2200: late, 2300: night},  # Tuesday
        {0: night, 1200: day, 1300: full, 2200: late, 2300: night},  # Wednesday
        {0: night, 1200: day, 1300: full, 2200: late, 2300: night},  # Thursday
        {0: night, 1200: day, 1300: full, 2200: late, 2300: night},  # Friday
        {0: night, 1200: day, 1300: full, 2200: late, 2300: night},  # Saturday
        {0: night, 1200: day, 1300: full, 2200: late, 2300: night},  # Sunday
    ]
    # Xmas/jingle jam adjustment
    if datetime.today().month == 12:
        for i in range(len(times)):
            del times[i][1300]
            del times[i][1200]
            times[i][1100] = xmas
    return times


last_state = None


# Create the callback to turn lights on/off and change colors at certain times
def callback():
    global last_state

    # Get map for today and current time
    times = get_times()
    time_map = times[datetime.today().weekday()]
    time = int(datetime.today().strftime('%H%M'))

    # Find most recent passed time in the map
    keys = sorted(time_map.keys())
    index = -1
    while index + 1 < len(keys) and keys[index + 1] <= time:
        index += 1
    state = keys[index]

    # Run the mapped state if not previously run
    run_callback = time_map[state]
    if last_state != run_callback:
        run_callback()
        last_state = run_callback


# Park a couple of bad fixtures
for f in dmx.get_fixtures_by_name_include('Shelf 3') \
         + dmx.get_fixtures_by_name_include('Key'):
    f.park()

# Enable the callback
dmx.ticker.add_callback(callback, 500)

# Debug
callbacks = {
    "night": night,
    "day": day,
    "full": full,
    "late": late,
    "xmas": xmas,
    "divoom-off": divoom_off,
    "divoom-on": divoom_on,
}
dmx.web_control(callbacks=callbacks)
# dmx.debug_control(callbacks)

# Close the controller once we're done
dmx.sleep_till_interrupt()
dmx.close()
