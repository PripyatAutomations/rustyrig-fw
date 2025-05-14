/*
 * rig control (CAT over websocket)
 */
if (!window.webui_inits) window.webui_inits = [];
window.webui_inits.push(function webui_rigctl_init() {
   let status = "OK";

   console.log("rigctl init: start");
   console.log("rigctl init: end. status=" + status);
});
