
var lastTimeLogged = 0;
var firingID = 0;
var segmentType = 0;

var pauseText = "❙❙";
var resumeText = "\u25b6";

function DrawGraph(parameters) {
    var canvas = document.getElementById("fullscreen");
    var ctx = canvas.getContext("2d");
    var width = canvas.width;
    var height = canvas.height;
    var pphundred = Math.round(height/16);
    var pphour = pphundred; // set the time axis such that 100 dph is a 45-degree line
    var dispSec = Math.round(3600 * width / pphour);

    ctx.translate(0,height);

    // draw temp lines
    ctx.strokeStyle = "#999999";
    ctx.fillStyle = "#999999";
    ctx.textBaseline = "bottom";
    ctx.textAlign = "right";
    ctx.font = "12pt sans-serif";
    var y = 0;
    var t = 0;

    ctx.beginPath();
    for ( y = 0, t = 0; y < height; y += pphundred, t += 100 ) {
        ctx.moveTo(0,-y); ctx.lineTo(width,-y); 
        ctx.fillText(t.toString()+"   ", width, -y);
    }
    ctx.stroke();

    function ScaleX(x) {
        return Math.round((x-(lastTimeLogged-dispSec)) * pphour / 3600 );
    }
    function ScaleY(y) {
        return -Math.round(y * pphundred / 100);
    }

    // draw seg lines
    ctx.strokeStyle = "#999900";
    var key = "";
    ctx.beginPath();
    for ( key in parameters.segments ) { 
        var xt = ScaleX(parseInt( key ));
        ctx.moveTo(xt,0); ctx.lineTo(xt,-height); 
    }
    ctx.stroke();

    // draw temp graph
    ctx.strokeStyle = "#333399";
    ctx.lineWidth = 3;
    var i = 0;
    ctx.beginPath();
    var x = ScaleX(parameters.temps[0][0]);
    var y = ScaleY(parameters.temps[0][1]);
    ctx.moveTo( x, y );
    for (i = 1; i < parameters.temps.length; ++i ) {
        var newX = ScaleX(parameters.temps[i][0]);
        var newY = ScaleY(parameters.temps[i][1]);
        if ( newX != x ) {
            x = newX;
            y = newY;
            ctx.lineTo( x, y );
        }
    }
    ctx.stroke(); 
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

    var request = new XMLHttpRequest;
    request.open( "GET", "/cgi-bin/log.cgi?sec=" + dispSec.toString() );
    request.responseType = "json";
    request.onload = function () {
        var response = request.response;
        DrawGraph( response);
        request.onload = null;
    }
    request.send();
}

function Update() {
    var canvas = document.getElementById("fullscreen");
    var width = parseInt(getComputedStyle(canvas).width);
    var height = parseInt(getComputedStyle(canvas).height); 
    if ( canvas.width != width ) {
        canvas.width = width;
        lastTimeLogged = 0;
    }
    if ( canvas.height != height ) { 
        canvas.height = height;
        lastTimeLogged = 0;
    }
    var request = new XMLHttpRequest;
    request.open( "GET", "/cgi-bin/status.cgi" );
    request.responseType = "json"; 
    request.onload = function () {
        var response = request.response;
        document.getElementById("pv").innerText = response.pv;
        document.getElementById("sv").innerText = response.sv;
        document.getElementById("elt").innerText = response.elapsedTime;
        document.getElementById("sct").innerText = response.plannedTime;
        firingID = response.firingID;
        segmentType = response.segmentType;
        var element = document.getElementById("pause").firstElementChild;
        var lightElement = document.getElementById("pauselight"); 
        if ( segmentType == 3 ) { // pause
            lightElement.className = "paused";
            element.innerText = resumeText;
            element.style.lineHeight="0.8";
        } 
        else {
            lightElement.className = "";
            element.innerText = pauseText;
            element.style.lineHeight="normal";
        }
        if ( lastTimeLogged != response.lastTime ) {
            lastTimeLogged = response.lastTime;
            UpdateGraph();
        }
        request.onload = null; 
    };
    request.send();
}

function SendCommand( cmd, p1, p2 ) {
    var request = new XMLHttpRequest;
    request.open( "GET", "/cgi-bin/command.cgi?cmd=" + cmd.toString() + "&p1=" + p1.toString() + "&p2=" + p2.toString);
    request.onload = function() { request.onload = null; }
    request.send();
}

function StopProgram() {
    if ( !firingID ) return;
    if ( !confirm("Stop the currently running program?")) return;
    SendCommand(2,0,0);
}

function PauseResumeProgram() {
    if ( segmentType == 3 ) { // pause
        SendCommand(6,0,0); // resume
    } 
    else {
        SendCommand(5,0,0); // pause
    }
}

function ShowProgramDialog() {
    document.getElementById("programs").style.visibility = "visible";
    FillProgramList();
}

function HideProgramDialog() {
    document.getElementById("programs").style.visibility = "hidden";
}

var selectedItem = null;
var selectedItemId = 0;

function SelectItem( event ) {
    var element = event.target;
    var id = element.getAttribute("data-id");

    if ( selectedItem ) {
        selectedItem.className="listitem";
    }
    selectedItem = element;
    selectedItemId = id;
    element.className = "listitem liselected";
}

function FillProgramList() {
    var request = new XMLHttpRequest;
    request.open( "GET", "/cgi-bin/programs.cgi");
    request.responseType = "json"; 
    request.onload = function() { 
        var response = request.response;
        var list = document.getElementById("list");
        list.innerText="";
        var i = 0;
        for ( i = 0; i < response.programs.length; ++i ) {
            var program = response.programs[i];
            var item = document.createElement("div");
            item.className = "listitem";
            item.setAttribute("data-id", program.id.toString()); 
            item.innerText = program.name;
            item.addEventListener("click", SelectItem);
            list.insertAdjacentElement("beforeend", item); 
        } 
    }
    request.send();
}

function GetSelectedId() {
    return selectedItemId;
}

function RunSelectedProgram() {
    if ( !GetSelectedId()) return;
    if ( firingID && !confirm("Stop the currently running program?")) return;
    SendCommand(4, GetSelectedId(), 1);
    HideProgramDialog();
} 

function ShowSetpointDialog() {
    document.getElementById("setpoint").style.visibility = "visible";
    FillSetpointEdit();
}

function FillSetpointEdit() {
    document.getElementById("setpoint_edit").value = document.getElementById("sv").innerText;
}

function HideSetpointDialog() {
    document.getElementById("setpoint").style.visibility = "hidden";   
}

function SetSetpoint() {
    var sp = document.getElementById("setpoint_edit").value;
    SendCommand( 3, sp, 0 );
    HideSetpointDialog();
}

function Setup() {
    document.getElementById("select").addEventListener("click", ShowProgramDialog);
    document.getElementById("stop").addEventListener("click", StopProgram);
    document.getElementById("set").addEventListener("click", ShowSetpointDialog);
    document.getElementById("pause").addEventListener("click", PauseResumeProgram);
    
    document.getElementById("back").addEventListener("click", HideProgramDialog);
    document.getElementById("run").addEventListener("click", RunSelectedProgram);
    
    document.getElementById("sp_ok").addEventListener("click", SetSetpoint);
    document.getElementById("sp_cancel").addEventListener("click",HideSetpointDialog);
    setInterval(Update,1000);
}

setTimeout( Setup, 1000);
