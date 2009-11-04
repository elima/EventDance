const GLib = imports.gi.GLib;
const Gio = imports.gi.Gio;
const MainLoop = imports.mainloop;
const Evd = imports.gi.Evd;
const Lang = imports.lang;

let service;

service = new Evd.Service ();
service.connect ("new-connection", function (self, socket) {
    socket.write ("hello world!\n");
    log ("new connection");
});

service.add_listener_inet ("*", 6666);

MainLoop.run ("main");

