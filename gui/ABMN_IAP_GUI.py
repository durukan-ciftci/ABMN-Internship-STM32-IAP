import tkinter as tk
from tkinter import ttk
from ttkthemes import ThemedTk 
import tkinterDnD as tkDND
import struct
import subprocess
import os
import tempfile
import serial
import threading
import time


app_serial_baudrate = 115200
app_serial_comport = "COM15"

root = ThemedTk()

root.geometry("600x900"); # width*height
root.title("ABMN_IAP_GUI") 

style = ttk.Style(root)
def on_close():
    global stop_terminal_thread
    stop_terminal_thread = True
    root.destroy()
    file_root.destroy()

#function for changing style
def transmit_run(comport, baudrate, teraterm_path, filepath): #change to baudrate
    ttl_content = f"""
connect '/C={comport}'
pause 5
setbaud {baudrate}
ymodemsend '{filepath}'
closett
"""
    temporary_ttl_file_adress = tempfile.gettempdir()
    temporary_ttl_file = os.path.join(temporary_ttl_file_adress,"autosend.ttl")
    
    with open(temporary_ttl_file, "w", encoding="utf-8") as teraterm_file:
        teraterm_file.write(ttl_content)
        
    subprocess.Popen(f'"{teraterm_path}" /M={temporary_ttl_file}')
    
    
def changer(theme):
    #change style
    style.theme_use(theme)
    #write style
    style_label.config(text = f'Theme:{theme}')

#function for dropping inside listbox
def drop_inside_listbox(event):
    
    if event.data not in file_listbox.get(0, tk.END ):
        file_listbox.insert("end", event.data)
    transmit_run(int(choose_comport_combobox.get()[4: len(choose_comport_combobox.get())]),int(choose_baudrate_combobox.get()), r'C:\Program Files (x86)\teraterm5\ttermpro.exe', event.data)

#function for selecting a path listed in listbox
def file_select_event(event):
    transmit_run(int(choose_comport_combobox.get()[4: len(choose_comport_combobox.get())]),int(choose_baudrate_combobox.get()), r'C:\Program Files (x86)\teraterm5\ttermpro.exe', event.widget.get(int(event.widget.curselection()[0])) )

#function for dropping inside textbox
def drop_inside_textbox(event):
    file_textbox.delete("1.0", "end")
    if event.data.endswith(".bin"):
        with open(event.data, "rb") as file:
            fileContent = file.read()
            for l in range(0,(len(fileContent)//4)*4 -16,16):
                #print(hex(l))
                #print(hex(l+16))
                ints = struct.unpack("<IIII", fileContent[l:l+16])
                
                file_textbox.insert("end", f"|Adress:  {l:08x}  |||")
                for i in ints:
                    file_textbox.insert("end", f"|  {i:08x}  |")
                file_textbox.insert("end", f"|\n")

            
            ints = struct.unpack("<"+"I"*((len(fileContent)//4)%4), fileContent[((len(fileContent)//4) * 4)-((len(fileContent)//4)%4)*4:((len(fileContent)//4) * 4)])
            file_textbox.insert("end", f"|Adress:  {((len(fileContent)//4) * 4)-((len(fileContent)//4)%4)*4:08x}  |||")
            for i in ints:
                file_textbox.insert("end", f"|  {i:08x}  |")
            file_textbox.insert("end", f"|")
            
def run_terminal():
    global terminal_frame, terminal_textbox
    terminal_frame.pack(pady = 10)
    ser = serial.Serial(app_serial_comport, app_serial_baudrate, timeout = 1)
    while True:
        if(ser.isOpen()):
            line = ser.readline()   # read a '\n' terminated line
            terminal_textbox.insert("end" ,line)
            terminal_textbox.see("end")
            time.sleep(0.1)


def close_terminal():
    global terminal_frame
    global terminal_textbox
    terminal_frame.pack_forget()
    terminal_textbox.delete('1.0', tk.END)
    
def erase_terminal():
    global terminal_textbox
    terminal_textbox.delete('1.0', tk.END)
    
    
stop_terminal_thread = False


#   create a menu
general_menu = tk.Menu()
root.config(menu = general_menu)

    
theme_menu = tk.Menu(general_menu, tearoff=0)
general_menu.add_cascade(label = "themes", menu = theme_menu)

#store included themes
themes = ttk.Style().theme_names()

#sub menu
for t in themes:
    theme_menu.add_command(label =  t, command = lambda t = t: changer(t))


Info_frame = ttk.Frame(root)
Info_frame.pack(pady = 5)

style_label = ttk.Label(Info_frame, text = "Theme:", font = ('Helvetica',8), background="bisque")
Info_frame.pack(side = "top", fill = "x")
style_label.pack(side = "right")

# widgets goes there
terminal_label = ttk.Label(root, text = "File Transfer Terminal", font = ('Helvetica',20), background="bisque")
terminal_label.pack(pady = 50)


Options_frame = ttk.Frame(root)
Options_frame.pack(pady = 5)

terminal_label = ttk.Label(Options_frame, text = "Select a COM Port", font = ('Helvetica',8), background="bisque")
terminal_label.grid(row = 1, column= 0, padx = 20)

terminal_label = ttk.Label(Options_frame, text = "Select a Baudrate", font = ('Helvetica',8), background="bisque")
terminal_label.grid(row = 1, column= 1)


comport_options = []
for i in range(1,100):
    comport_options.append(f"COM {i}")

comport = tk.StringVar()
choose_comport_combobox = ttk.Combobox(Options_frame, width = 10, values = comport_options, height = 6, textvariable = comport, background="bisque")

    
choose_comport_combobox.grid(row = 2, column= 0, padx = 20)
choose_comport_combobox.set("COM 11")

baudrate_options = [
    "600",
    "1200",
    "2400",
    "4800",
    "9600",
    "14400",
    "19200",
    "38400",
    "57600",
    "115200",
    "230400",
    "460800",
    "921600",
    ]


baudrate = tk.StringVar()
choose_baudrate_combobox = ttk.Combobox(Options_frame, width = 10, values = baudrate_options, height = 6, textvariable=baudrate, background="bisque")

    
choose_baudrate_combobox.grid(row = 2, column= 1)
choose_baudrate_combobox.set("115200")

#frame for terminal
terminal_open_close_frame = ttk.Frame(root)
terminal_open_close_frame.pack(pady = 50)

var = tk.StringVar(root, "Closed")

terminal_thread = threading.Thread(target=run_terminal)

open_terminal_radio = ttk.Radiobutton(terminal_open_close_frame, text = "Open Application Terminal", variable = var, value = "Open", command = lambda: terminal_thread.start() if (not terminal_thread.is_alive()) else terminal_frame.pack(pady = 10)) 
open_terminal_radio.grid(row = 0, column= 0, padx = 20)

close_terminal_radio = ttk.Radiobutton(terminal_open_close_frame, text = "Close Application Terminal", variable = var, value = "Closed", command  = close_terminal) 
close_terminal_radio.grid(row = 0, column= 1)



terminal_frame = ttk.Frame(root)
terminal_frame.pack_forget()

terminal_erase_button = ttk.Button(terminal_frame, text = "Erase Terminal", command = erase_terminal)
terminal_erase_button.pack(fill = tk.X)

scrollbar = tk.Scrollbar(terminal_frame)
terminal_textbox = tk.Text(terminal_frame, background = "bisque", yscrollcommand = scrollbar.set)
scrollbar.config(command=terminal_textbox.yview)
scrollbar.pack(side = tk.RIGHT, fill = tk.Y)
terminal_textbox.pack(side = tk.LEFT, fill = tk.X)



#root for handling files
file_root = tkDND.Tk()

file_root.geometry("820x600"); # width*height
file_root.title("File Analyzer") 

file_listbox = tk.Listbox(file_root, selectmode=tk.SINGLE, background = "bisque")
file_listbox.pack(fill = tk.X)
file_listbox.register_drop_target("*")
file_listbox.bind("<<Drop>>", drop_inside_listbox)
file_listbox.bind("<<ListboxSelect>>", file_select_event)
()
file_textbox = tk.Text(file_root)
file_textbox.pack()
file_textbox.register_drop_target("*")
file_textbox.bind("<<Drop>>", drop_inside_textbox)


root.protocol("WM_DELETE_WINDOW", on_close)
file_root.protocol("WM_DELETE_WINDOW", on_close)
#execute


                     
root.mainloop();
file_root.mainloop()



