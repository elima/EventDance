const MainLoop = imports.mainloop;
const Evd = imports.gi.Evd;
const Lang = imports.lang;

let service;
let listener;

service = new Evd.Service ();
service.connect ("new-connection", function (self, socket) {
    socket.write ("hello world!\n");
    log ("new connection");

    listener.close ();
    service.remove_listener (listener);
});
service.set_on_read (function (group, socket) {
    log ("receive!");

    let [data, size, wait] = socket.read (1000);

    if (data == "quit\r\n")
      MainLoop.quit ("main");
  });

listener = service.listen ("0.0.0.0:6666");

MainLoop.run ("main");
