
using System;
using System.IO;
using System.Text;
using System.Drawing;
using System.Windows.Forms;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.Diagnostics;

internal sealed class MonitorForm : Form {
    [DllImport("user32.dll", SetLastError=true)]
    static extern bool RegisterHotKey(IntPtr hWnd, int id, uint modifiers, uint key);
    [DllImport("user32.dll", SetLastError=true)]
    static extern bool UnregisterHotKey(IntPtr hWnd, int id);
    [DllImport("user32.dll", SetLastError=true)]
    static extern IntPtr SetWindowsHookEx(int idHook, KeyboardProc callback, IntPtr module, uint threadId);
    [DllImport("user32.dll")]
    static extern bool UnhookWindowsHookEx(IntPtr hook);
    [DllImport("user32.dll")]
    static extern IntPtr CallNextHookEx(IntPtr hook, int code, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")]
    static extern short GetAsyncKeyState(int key);
    [DllImport("kernel32.dll", CharSet=CharSet.Auto)]
    static extern IntPtr GetModuleHandle(string name);
    [DllImport("user32.dll")]
    static extern uint MapVirtualKey(uint code, uint mapType);
    [DllImport("user32.dll")]
    static extern int GetKeyNameText(int lParam, StringBuilder buffer, int size);

    [DllImport("user32.dll")]
    static extern IntPtr GetSystemMenu(IntPtr hWnd, bool revert);
    [DllImport("user32.dll")]
    static extern uint EnableMenuItem(IntPtr menu, uint item, uint flags);
    const uint SC_MINIMIZE=0xF020, MF_BYCOMMAND=0, MF_GRAYED=1;
    const int WM_SYSCOMMAND=0x0112;

    delegate IntPtr KeyboardProc(int code, IntPtr wParam, IntPtr lParam);
    [StructLayout(LayoutKind.Sequential)]
    struct KeyboardEvent {
        public uint vkCode, scanCode, flags, time;
        public IntPtr extraInfo;
    }
    const int WH_KEYBOARD_LL=13, WM_KEYDOWN=0x100, WM_KEYUP=0x101,
              WM_SYSKEYDOWN=0x104, WM_SYSKEYUP=0x105, WM_HOTKEY=0x312;
    const uint MOD_SHIFT=4, MOD_NOREPEAT=0x4000;
    readonly KeyboardProc callback;
    readonly HashSet<uint> pressed=new HashSet<uint>();
    readonly Dictionary<int,string> hotkeyNames=new Dictionary<int,string>();
    readonly Dictionary<string,DateTime> lastLogged=new Dictionary<string,DateTime>();
    readonly TextBox log=new TextBox();
    readonly Label status=new Label();
    readonly Button start=new Button(), stop=new Button(), save=new Button(), clear=new Button();
    readonly CheckBox alwaysOnTop=new CheckBox();
    IntPtr hook=IntPtr.Zero;
    bool active=false;
    int nextId=1;
    int failedRegistrations=0;
    readonly string[] symbols={"~","!","@","#","$","%","^","&","*","(",")","_","+","{","}","|",":","\"","<",">","?"};
    readonly uint[] symbolKeys={0xC0,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x30,0xBD,0xBB,0xDB,0xDD,0xDC,0xBA,0xDE,0xBC,0xBE,0xBF};

    public MonitorForm() {
        callback=OnKeyboard;
        Text="KeyMonitor Combined | Visible Monitoring";
        StartPosition=FormStartPosition.CenterScreen;
        // Compact initial layout, sized for full timestamps and long key combinations.
        ClientSize=new Size(480,335);
        MinimumSize=new Size(480,335);
        var info=new Label {
            Text="General keyboard events + Windows shortcuts. Start requires consent; minimizing stops monitoring.",
            Left=12,Top=10,Width=456,Height=32,
            Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right
        };
        Controls.Add(info);
        alwaysOnTop.Text="Always on top";
        alwaysOnTop.SetBounds(12,43,200,23);
        alwaysOnTop.Checked=true;
        TopMost=true;
        alwaysOnTop.CheckedChanged+=(s,e)=>TopMost=alwaysOnTop.Checked;
        Controls.Add(alwaysOnTop);
        start.Text="Start (I consent)";start.SetBounds(12,72,220,30);
        stop.Text="Stop";stop.SetBounds(244,72,224,30);
        save.Text="Save Log";save.SetBounds(12,108,220,30);
        clear.Text="Clear";clear.SetBounds(244,108,224,30);
        start.Click+=(s,e)=>StartMonitoring();
        stop.Click+=(s,e)=>StopMonitoring();
        save.Click+=(s,e)=>SaveLog();
        clear.Click+=(s,e)=>log.Clear();
        Controls.AddRange(new Control[]{start,stop,save,clear});
        status.SetBounds(12,144,456,40);
        status.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;
        status.Font=new Font(SystemFonts.DefaultFont,FontStyle.Bold);
        Controls.Add(status);
        log.Multiline=true;log.ReadOnly=true;log.WordWrap=false;
        log.ScrollBars=ScrollBars.Both;log.SetBounds(12,190,456,133);
        log.Anchor=AnchorStyles.Top|AnchorStyles.Bottom|AnchorStyles.Left|AnchorStyles.Right;
        log.Font=new Font("Consolas",8.5f);
        Controls.Add(log);
        UpdateStatus();
    }
    void UpdateStatus() {
        status.Text=active
            ? "MONITORING ACTIVE | General keyboard + "+hotkeyNames.Count+" registered shortcuts"
              +(failedRegistrations>0?" | "+failedRegistrations+" registrations unavailable":"")
            : "STOPPED | No keyboard monitoring";
        status.ForeColor=active?Color.DarkRed:Color.DarkGreen;
        start.Enabled=!active;stop.Enabled=active;
        // Keep the normal minimize control available only while stopped.
        MinimizeBox=!active;
        IntPtr menu=GetSystemMenu(Handle,false);
        if(menu!=IntPtr.Zero)
            EnableMenuItem(menu,SC_MINIMIZE,MF_BYCOMMAND|(active?MF_GRAYED:0));
    }
    void Write(string entry) {
        if(log.IsDisposed)return;
        log.AppendText(DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff")+"  "+entry+Environment.NewLine);
    }
    bool IsModifier(uint vk) {
        return vk==0x10||vk==0x11||vk==0x12||
               vk==0xA0||vk==0xA1||vk==0xA2||vk==0xA3||
               vk==0xA4||vk==0xA5||vk==0x5B||vk==0x5C;
    }
    string KeyName(uint vk,uint scan,uint flags) {
        switch(vk) {
            case 0xA0:return "LShift";case 0xA1:return "RShift";
            case 0xA2:return "LCtrl";case 0xA3:return "RCtrl";
            case 0xA4:return "LAlt";case 0xA5:return "RAlt";
            case 0x5B:return "LWin";case 0x5C:return "RWin";
            case 0x10:return "Shift";case 0x11:return "Ctrl";case 0x12:return "Alt";
            case 0x08:return "Backspace";case 0x09:return "Tab";case 0x0D:return "Enter";
            case 0x1B:return "Esc";case 0x20:return "Space";case 0x21:return "PageUp";
            case 0x22:return "PageDown";case 0x23:return "End";case 0x24:return "Home";
            case 0x25:return "Left";case 0x26:return "Up";case 0x27:return "Right";
            case 0x28:return "Down";case 0x2D:return "Insert";case 0x2E:return "Delete";
            case 0x14:return "CapsLock";case 0x90:return "NumLock";case 0x91:return "ScrollLock";
        }
        if(vk>=0x70&&vk<=0x7B)return "F"+(vk-0x6F);
        if(vk>=0x60&&vk<=0x69)return "[NUM] "+(vk-0x60);
        if(vk==0x6A)return "[NUM] *";
        if(vk==0x6B)return "[NUM] +";
        if(vk==0x6D)return "[NUM] -";
        if(vk==0x6E)return "[NUM] .";
        if(vk==0x6F)return "[NUM] /";
        if(vk>=0x30&&vk<=0x5A)return ((char)vk).ToString();
        var b=new StringBuilder(80);
        int code=(int)(scan<<16);
        if((flags&1)!=0)code|=1<<24;
        if(GetKeyNameText(code,b,b.Capacity)>0)return b.ToString();
        return "VK_"+vk;
    }
    string ModifierPrefix() {
        var m=new List<string>();
        if((GetAsyncKeyState(0xA2)&0x8000)!=0)m.Add("LCtrl");
        else if((GetAsyncKeyState(0xA3)&0x8000)!=0)m.Add("RCtrl");
        else if((GetAsyncKeyState(0x11)&0x8000)!=0)m.Add("Ctrl");
        if((GetAsyncKeyState(0xA0)&0x8000)!=0)m.Add("LShift");
        else if((GetAsyncKeyState(0xA1)&0x8000)!=0)m.Add("RShift");
        else if((GetAsyncKeyState(0x10)&0x8000)!=0)m.Add("Shift");
        if((GetAsyncKeyState(0xA4)&0x8000)!=0)m.Add("LAlt");
        else if((GetAsyncKeyState(0xA5)&0x8000)!=0)m.Add("RAlt");
        else if((GetAsyncKeyState(0x12)&0x8000)!=0)m.Add("Alt");
        if((GetAsyncKeyState(0x5B)&0x8000)!=0)m.Add("LWin");
        if((GetAsyncKeyState(0x5C)&0x8000)!=0)m.Add("RWin");
        return String.Join(" + ",m.ToArray());
    }
    bool IsRegisteredShortcut(uint vk) {
        if(vk>=0x70&&vk<=0x7B)return true;
        bool shift=(GetAsyncKeyState(0x10)&0x8000)!=0;
        if(!shift)return false;
        foreach(uint key in symbolKeys)if(vk==key)return true;
        return false;
    }
    IntPtr OnKeyboard(int code,IntPtr wp,IntPtr lp) {
        if(code>=0&&active&&Visible&&WindowState!=FormWindowState.Minimized) {
            var e=(KeyboardEvent)Marshal.PtrToStructure(lp,typeof(KeyboardEvent));
            int kind=wp.ToInt32();
            if(kind==WM_KEYUP||kind==WM_SYSKEYUP)pressed.Remove(e.vkCode);
            else if(kind==WM_KEYDOWN||kind==WM_SYSKEYDOWN) {
                bool first=pressed.Add(e.vkCode);
                // Modifier keys only log on the first physical key-down.
                if(!IsModifier(e.vkCode)||first) {
                    // Registered shortcuts are logged via WM_HOTKEY, not a second time here.
                    // For unavailable registrations, the hook still reports the key.
                    if(!IsRegisteredShortcut(e.vkCode)||!IsRegisteredSuccessfully(e.vkCode)) {
                        string modifier=ModifierPrefix();
                        string key=KeyName(e.vkCode,e.scanCode,e.flags);
                        Write("KEY  "+(modifier.Length>0?modifier+" + ":"")+key);
                    }
                }
            }
        }
        return CallNextHookEx(hook,code,wp,lp);
    }
    readonly HashSet<uint> registeredFunctionKeys=new HashSet<uint>();
    readonly HashSet<uint> registeredSymbolKeys=new HashSet<uint>();
    bool IsRegisteredSuccessfully(uint vk) {
        if(vk>=0x70&&vk<=0x7B)return registeredFunctionKeys.Contains(vk);
        return registeredSymbolKeys.Contains(vk);
    }
    void Register(string name,uint vk,uint mods) {
        int id=nextId++;
        if(RegisterHotKey(Handle,id,mods|MOD_NOREPEAT,vk)) {
            hotkeyNames[id]=name;
            if(mods==0)registeredFunctionKeys.Add(vk);
            else registeredSymbolKeys.Add(vk);
        } else {
            failedRegistrations++;
            Write("UNAVAILABLE shortcut "+name+" (Windows error "+Marshal.GetLastWin32Error()+")");
        }
    }
    void StartMonitoring() {
        if(active)return;
        if(MessageBox.Show(this,
           "This program monitors keyboard activity across Windows applications, including potentially sensitive information.\n\nMonitoring remains visible and automatically stops when minimized.\n\nStart monitoring?",
           "Keyboard Monitoring Consent",MessageBoxButtons.YesNo,MessageBoxIcon.Warning,
           MessageBoxDefaultButton.Button2)!=DialogResult.Yes)return;
        pressed.Clear();failedRegistrations=0;nextId=1;
        registeredFunctionKeys.Clear();registeredSymbolKeys.Clear();
        // Start the hook first so a failed hook does not reserve shortcuts.
        using(var p=Process.GetCurrentProcess()) {
            string name=p.MainModule==null?null:p.MainModule.ModuleName;
            hook=SetWindowsHookEx(WH_KEYBOARD_LL,callback,GetModuleHandle(name),0);
        }
        if(hook==IntPtr.Zero) {
            MessageBox.Show(this,"Keyboard hook failed. Windows error: "+Marshal.GetLastWin32Error(),
                            "KeyMonitor",MessageBoxButtons.OK,MessageBoxIcon.Error);
            return;
        }
        active=true;
        for(int i=1;i<=12;i++)Register("F"+i,(uint)(0x6F+i),0);
        for(int i=0;i<symbols.Length;i++)Register(symbols[i],symbolKeys[i],MOD_SHIFT);
        Write("SESSION START | General keyboard monitoring + registered shortcuts");
        UpdateStatus();
    }
    void StopMonitoring() {
        if(!active)return;
        active=false;
        if(hook!=IntPtr.Zero) {UnhookWindowsHookEx(hook);hook=IntPtr.Zero;}
        foreach(int id in hotkeyNames.Keys)UnregisterHotKey(Handle,id);
        hotkeyNames.Clear();registeredFunctionKeys.Clear();registeredSymbolKeys.Clear();
        pressed.Clear();
        Write("SESSION STOP");
        UpdateStatus();
    }
    void SaveLog() {
        using(var d=new SaveFileDialog {Filter="Text files (*.txt)|*.txt",
                FileName="KeyMonitor_Combined_Log.txt",DefaultExt="txt"}) {
            if(d.ShowDialog(this)!=DialogResult.OK)return;
            try {File.WriteAllText(d.FileName,log.Text,Encoding.UTF8);}
            catch(Exception e) {MessageBox.Show(this,e.Message,"Save failed",
                    MessageBoxButtons.OK,MessageBoxIcon.Error);}
        }
    }
    protected override void WndProc(ref Message m) {
        // Reject application-window minimize commands while monitoring is active.
        if(active && m.Msg==WM_SYSCOMMAND &&
           ((m.WParam.ToInt64() & 0xFFF0)==SC_MINIMIZE))return;
        if(m.Msg==WM_HOTKEY&&active) {
            string name;
            if(hotkeyNames.TryGetValue(m.WParam.ToInt32(),out name))
                Write("SHORTCUT  "+name);
        }
        base.WndProc(ref m);
    }
    protected override void OnResize(EventArgs e) {
        base.OnResize(e);
        if(WindowState==FormWindowState.Minimized)StopMonitoring();
    }
    protected override void OnVisibleChanged(EventArgs e) {
        base.OnVisibleChanged(e);
        if(!Visible)StopMonitoring();
    }
    protected override void OnFormClosing(FormClosingEventArgs e) {
        StopMonitoring();
        base.OnFormClosing(e);
    }
    [STAThread]
    static void Main() {
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new MonitorForm());
    }
}
