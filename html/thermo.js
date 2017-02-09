
var request = new XMLHttpRequest;
var lastTimeLogged = 0;

function ShowProgramDialog() {
    document.getElementById("programs").style.visibility = "visible";
}

function HideProgramDialog() {
    document.getElementById("programs").style.visibility = "hidden";
}

function UpdateGraph() {
    var canvas = document.getElementById("fullscreen");
    var width = parseInt(getComputedStyle(canvas).width);
    var height = parseInt(getComputedStyle(canvas).height); 
    if ( canvas.width != width ) canvas.width = width;
    if ( canvas.height != height ) canvas.height = height;

    var pphundred = height/16;
    var pphour = pphundred; // set the time axis such that 100 dph is a 45-degree line
    var dispSec = Math.round(3600 * width / pphour);

    request.open( "GET", "/cgi-bin/logs.cgi?sec=" + dispSec.toString() );
    request.responseType = "json";
    request.onreadystatechange = function () {
        if(request.readyState === XMLHttpRequest.DONE && request.status === 200) {
            var response = request.response;
        }
    }
    request.send();
}

function Update() {
    request.open( "GET", "/cgi-bin/status.cgi" );
    request.responseType = "json"; 
    request.onreadystatechange = function () {
        if(request.readyState === XMLHttpRequest.DONE && request.status === 200) {
            var response = request.response;
            document.getElementById("pv").innerText = response["pv"];
            document.getElementById("sv").innerText = response["sv"];
            document.getElementById("elt").innerText = response["elapsedTime"];
            document.getElementById("sct").innerText = response["plannedTime"];
            if ( lastTimeLogged != response.lastTime ) {
                lastTimeLogged = response.lastTime;
                UpdateGraph();
            } 
        }
    };
    request.send();
}

function Setup() {
    document.getElementById("select").addEventListener("click", ShowProgramDialog);
    document.getElementById("back").addEventListener("click", HideProgramDialog);
    setInterval(Update,1000);
}

setTimeout( Setup, 1000);
