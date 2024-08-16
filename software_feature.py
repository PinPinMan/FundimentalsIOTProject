"""
Set up a Telegram bot to interact with an Arduino device for tracking progress and setting custom time. 
Includes functions for sending messages to Arduino, handling commands, and sending email summaries.
"""

import serial
import time
import requests
from telegram import Update, Bot, InlineKeyboardButton, InlineKeyboardMarkup
from telegram.ext import Application, CommandHandler, CallbackQueryHandler, ContextTypes, MessageHandler, filters, ConversationHandler, CallbackContext
from io import BytesIO
from selenium import webdriver
from PIL import Image
from selenium.webdriver.chrome.options import Options
import requests
import json


# ========================= WayPoint API Configuration =========================
WAYPOINT_API_KEY_USERNAME = 'WAYPOINT_API_KEY_USERNAME'
WAYPOINT_API_KEY_PASSWORD= 'WAYPOINT_API_KEY_PASSWORD'

waypoint_url = 'https://live.waypointapi.com/v1/email_messages'
waypoint_headers = {"Content-Type": "application/json"}
waypoint_auth = (WAYPOINT_API_KEY_USERNAME, WAYPOINT_API_KEY_PASSWORD)

# ========================= Telegram Bot Configuration =========================
TOKEN = 'TelegramBotTOKEN'
BOT_USERNAME = '@BotUsername'
CHAT_ID = '-0000000000'
THINGSPEAK_CHANNEL_ID = "0000000"
THINGSPEAK_READ_API_KEY = "THINGSPEAK_READ_API_KEY"


# ========================= Arduino Serial Configuration =========================
arduino_port = 'COM6'  # Replace with your Arduino port
baud_rate = 9600
bot = Bot(token=TOKEN)
choice = {}
PeakPacerSummary = {
    "Green": {
        "count": 0
    },
    "Yellow": {
        "count": 0
    },
    "Red": {
        "count": 0
    },
    "Custom": {
        "count": 0
    }
}
log = []
sendEmailFlag = False
customTime = 10

def setup_serial():
    """
    Set up a serial connection with an Arduino board.
    @return The serial connection object.
    """
    try:
        arduino = serial.Serial(arduino_port, baud_rate)
        time.sleep(2)  # Wait for the connection to initialize
        return arduino
    except serial.serialutil.SerialException as e:
        print(f"Error: {e}")
        return None


# Set up serial communication
arduino = setup_serial()


# ========================= Function to send message to Arduino =========================
def send_to_arduino(message):
    """
    Send a message to the Arduino if it is connected, otherwise print a message indicating that the Arduino is not connected.
    @param message - The message to send to the Arduino.
    """
    if arduino:
        arduino.write(message.encode("utf-8"))
    else:
        print("Arduino is not connected")


def read_from_arduino(arduino):
    """
    Read data from the Arduino serial connection and update the PeakPacerSummary dictionary if a "Summary" message is received.
    @param arduino - the Arduino serial connection
    @return The output received from the Arduino, or None if no data is available.
    """
    if arduino.in_waiting > 0:
        output = arduino.readline().decode("utf-8").strip()
        if output.find("Summary") != -1:
            PeakPacerSummary[f'{output[output.find("(")+1:output.find("|")]}']['count'] += 1
        return output
    return None


# ========================= Telegram Command Handlers (About) =========================
async def about_command(update: Update, context: ContextTypes.DEFAULT_TYPE):
    await update.message.reply_text("Welcome to your Peak Pacer!!!\nYou can Track your progress & Set your own custom time.\n\nCommands:\n- /track_progress\n- /set_custom_time\n- /show_serial_monitor\n- /email_summary\n")


# ========================= Telegram Command Handlers (Track Progress) =========================
async def track_progress(update: Update, context: ContextTypes.DEFAULT_TYPE) -> None:
    """
    Asynchronously track the progress of an update using the given context and update information.
    @param update - The update information
    @param context - The context type for the update
    @return None
    """
    keyboard = [
        [
            InlineKeyboardButton("Time Taken", callback_data='Time Taken_0'),
            InlineKeyboardButton("Count", callback_data='Count_4'),
        ]
    ]
    reply_markup = InlineKeyboardMarkup(keyboard)
    await update.message.reply_text('Choose an option:', reply_markup=reply_markup)


async def graph_mode(update: Update, context: ContextTypes.DEFAULT_TYPE) -> None:
    """
    Define an asynchronous function to handle graph mode updates in a chat application.
    @param update - The update object containing information about the update.
    @param context - The context object containing information about the context of the update.
    @return None
    This function creates an inline keyboard with options for different game modes (Easy, Medium, Hard, Custom) and sends it as a reply to the user. If the update is triggered by a callback query, it edits the message with the new keyboard, otherwise, it sends a new message with the keyboard.
    """
    keyboard = [
        [
            InlineKeyboardButton("Easy", callback_data='Easy_1'),
            InlineKeyboardButton("Medium", callback_data='Medium_2'),
        ],
        [
            InlineKeyboardButton("Hard", callback_data='Hard_3'),
            InlineKeyboardButton("Custom", callback_data='Custom_4'),
        ]
    ]
    reply_markup = InlineKeyboardMarkup(keyboard)
    if update.callback_query:
        await update.callback_query.edit_message_text('Choose a mode:', reply_markup=reply_markup)
    else:
        await update.message.reply_text('Choose a mode:', reply_markup=reply_markup)


async def button(update: Update, context: ContextTypes.DEFAULT_TYPE) -> None:
    """
    Asynchronously handle button clicks in a Telegram bot interface.
    @param update - The update object containing information about the button click.
    @param context - The context object for the bot.
    @return None
    """
    query = update.callback_query
    await query.answer()

    if "type" not in choice:
        # First level selection: Type
        choice["type"] = query.data

        # Now show the mode selection
        await graph_mode(update, context)
    else:
        # Second level selection: Mode
        choice["mode"] = query.data
        selected_type = choice['type'].split("_")[0]
        selected_mode = choice['mode'].split("_")[0]

        graph_url = get_graph_url()
        image = render_url_to_image(graph_url)
        image_byte_array = BytesIO()
        image.save(image_byte_array, format='PNG')
        image_byte_array.seek(0)

        await bot.send_photo(chat_id=CHAT_ID, photo=image_byte_array)
        await query.edit_message_text(f"Selected: [{selected_type} - {selected_mode}]")

        # Reset the choice for the next selection
        choice.clear()


# Function to get the ThingSpeak graph URL
def get_graph_url():
    """
    Generate the URL for a graph based on the choice of chart type and mode.
    @return The URL for the graph
    """
    chart_num = int(choice["type"].split("_")[1]) + \
        int(choice["mode"].split("_")[1])
    print(f"Chart Number: {chart_num}")
    graph_url = f"https://thingspeak.com/channels/{THINGSPEAK_CHANNEL_ID}/charts/{chart_num}?api_key={THINGSPEAK_READ_API_KEY}&width=600&height=400"
    return graph_url

# Function to render HTML page to an image using Selenium
def render_url_to_image(url):
    """
    Render a webpage from a given URL and return it as an image.
    @param url - The URL of the webpage to render
    @return The rendered webpage as an image
    """
    options = Options()
    options.headless = True
    driver = webdriver.Chrome(options=options)
    driver.get(url)

    # Wait for the page to load and render
    time.sleep(2)

    screenshot = driver.get_screenshot_as_png()
    driver.quit()

    image = Image.open(BytesIO(screenshot))
    return image


# ========================= Telegram Command Handlers (Set Custom Time) =========================
WAITING_FOR_NUMBER = 1


async def set_custom_time(update: Update, context: CallbackContext) -> int:
    """
    Set a custom time for a conversation state and prompt the user to enter a number of seconds.
    @param update - The update object containing the message.
    @param context - The context of the callback.
    @return The conversation state constant WAITING_FOR_NUMBER.
    """
    global customTime
    context.user_data['conversation_state'] = WAITING_FOR_NUMBER
    await update.message.reply_text(f'CustomTime Currently ({customTime}sec)\nEnter number of seconds: (reply to me)')
    return WAITING_FOR_NUMBER


async def number_input(update: Update, context: CallbackContext) -> int:
    """
    Define an asynchronous function that handles number input from a user in a chat context.
    @param update: Update - The incoming update from the chat.
    @param context: CallbackContext - The context of the chat.
    @return int - The number input by the user.
    """
    global customTime
    user_input = update.message.text
    if user_input.isdigit():
        number = int(user_input)
        send_to_arduino(f'NewCustomTime_{number}')
        time.sleep(1)
        last_output = read_from_arduino(arduino)
        print(last_output)
        while last_output != None:
            last_output = read_from_arduino(arduino)
            print("Arduino >>", last_output)
            if last_output == "Changing Custom Time":
                await update.message.reply_text(f'Set Custom Time to {number} seconds!')
                customTime = number
                context.user_data['conversation_state'] = None
                return ConversationHandler.END

        await update.message.reply_text('Arduino NOT in Changing Custom Time Mode!!')
        context.user_data['conversation_state'] = None
        return ConversationHandler.END
    else:
        await update.message.reply_text('Invalid input! E.g. (15, 30, 45, 60)')
        return WAITING_FOR_NUMBER


# ========================= Telegram Command Handlers (Show Serial Monitor) =========================
async def show_serial_monitor(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """
    Display the serial monitor in the terminal and send an email if needed.
    @param update - The update object
    @param context - The context object
    @return None
    """
    global sendEmailFlag
    global log
    if not sendEmailFlag:
        log = []
    last_output = read_from_arduino(arduino)
    print(last_output)
    while last_output != None:
        previous_output = last_output
        last_output = read_from_arduino(arduino)
        log.append(f' >> {previous_output}')
        print(f'Arduino >> {previous_output}')

    sendEmailFlag = False
    await update.message.reply_text("Displaying Serial Monitor in Terminal...\n\n" + "\n".join(log))


# ========================= Telegram Command Handlers (Email Summary) =========================
async def send_email_summary(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """
    Send an email summary using Waypoint API with the provided template and data.
    @param update - The update object
    @param context - The context object
    @return None
    """
    global sendEmailFlag
    sendEmailFlag = True
    waypoint_data = {
        "templateId": "wptemplate_6dePMqrrQtxQ295j",
        "to": "mashturbatess@gmail.com",
        "variables": PeakPacerSummary
    }
    response = requests.post(waypoint_url, headers=waypoint_headers, auth=waypoint_auth, data=json.dumps(waypoint_data))
    print(response.json())
    await update.message.reply_text("Sending Email Summary...")

# ========================= Error Handler =========================
async def error(update: Update, context: ContextTypes.DEFAULT_TYPE):
    print(f'Update {update} caused error {context.error}')


# ========================= Start Telegram Bot =========================
def send_message(text):
    """
    Send a message using the Telegram bot API to a specific chat ID with the provided text.
    @param text - The text message to be sent
    @return The JSON response from the API after sending the message.
    """
    url_req = f"https://api.telegram.org/bot{TOKEN}/sendMessage?chat_id={CHAT_ID}&text={text}"
    results = requests.get(url_req)
    return results.json()


send_message("Starting Bot...")


# ========================= Running the Telegram Bot (MAIN) =========================
if __name__ == "__main__":
    print("Starting Bot...")
    app = Application.builder().token(TOKEN).build()

    conv_handler = ConversationHandler(
        entry_points=[CommandHandler("set_custom_time", set_custom_time)],
        states={
            WAITING_FOR_NUMBER: [MessageHandler(filters.TEXT & ~filters.COMMAND, number_input)],
        },
        fallbacks=[]
    )

    # Add the ConversationHandler first
    app.add_handler(conv_handler)

    # Then add other handlers
    app.add_handler(CommandHandler("about", about_command))
    app.add_handler(CommandHandler("track_progress", track_progress))
    app.add_handler(CommandHandler("set_custom_time", set_custom_time))
    app.add_handler(CommandHandler("show_serial_monitor", show_serial_monitor))
    app.add_handler(CommandHandler("email_summary", send_email_summary))
    app.add_handler(CallbackQueryHandler(button))

    # Errors
    app.add_error_handler(error)

    # Start polling
    app.run_polling(poll_interval=3)
