//Note that this script assumes the sample rate is 1 sample / minute WP (c) 2025
var tValues = [];
var xValues = [];
var yValues = [];
var zValues = [];
var bValues = [];

const myChart = new Chart("myChart", {
  type: "line",
  data: {
    labels: tValues,
    datasets: [
	{
	  label: "Mag X",
      fill: false,
      lineTension: 0.3,
      backgroundColor: "rgba(255,0,0,1.0)",
      borderColor: "rgba(255,0,0,0.5)",
      data: xValues,
	  pointRadius: 1
    },
	{
	  label: "Mag Y",
      fill: false,
      lineTension: 0.3,
      backgroundColor: "rgba(0,255,0,1.0)",
      borderColor: "rgba(0,255,0,0.5)",
      data: yValues,
	  pointRadius: 1
    },
	{
	  label: "Mag Z",
      fill: false,
      lineTension: 0.3,
      backgroundColor: "rgba(0,0,255,1.0)",
      borderColor: "rgba(0,0,255,0.5)",
      data: zValues,
	  pointRadius: 1
    },
	{
	  label: "Mag B",
	  hidden: true,
      fill: false,
      lineTension: 0.3,
      backgroundColor: "rgba(0,0,0,1.0)",
      borderColor: "rgba(0,0,0,0.5)",
      data: bValues,
	  pointRadius: 1
    }]
  },
  options: {
	animation: false,
    responsive: true,
    legend: {display: false},
    scales: {
      x: {
        title: {
          display: true,
          text: new Date(Date.now()).toString(),
        }
      },
      y: {
        title: {
          display: true,
          text: 'nT'
        }
      }
    }
  }
});

var Socket;
var slider = document.getElementById('ID_MODE');
var output = document.getElementById('ID_MODE_VALUE');
slider.addEventListener('change', slider_changed);

function init() {
  Socket = new WebSocket('ws://' + window.location.hostname + ':81/');
  Socket.onmessage = function(event) { processCommand(event); };
}

function slider_changed () {
  var l_mode = slider.value;
  console.log(l_mode);
  //var msg = { type: "plotdata", value: l_mode};
  //Socket.send(JSON.stringify(msg));
  updatechart();  
}

function updatechart() {
	if (slider.value == 2) {
		const xmean = xValues.reduce((a, b) => a + b, 0) / xValues.length;
		myChart.data.datasets[0].data = xValues.map(x => x - xmean);
		const ymean = yValues.reduce((a, b) => a + b, 0) / yValues.length;
		myChart.data.datasets[1].data = yValues.map(x => x - ymean);
		const zmean = zValues.reduce((a, b) => a + b, 0) / zValues.length;
		myChart.data.datasets[2].data = zValues.map(x => x - zmean);
		const bmean = bValues.reduce((a, b) => a + b, 0) / bValues.length;
		myChart.data.datasets[3].data = bValues.map(x => x - bmean);
	}
	else {
		myChart.data.datasets[0].data = xValues;
		myChart.data.datasets[1].data = yValues;
		myChart.data.datasets[2].data = zValues;
		myChart.data.datasets[3].data = bValues;
	}
	var d = Date.now();
	for(var i = 0, length = zValues.length; i < length; i++) {
		tValues[i] = new Date(d - (length-1-i) * 60 * 1000).toTimeString().slice(0, 5);
	}
	myChart.update();
}
	
function processCommand(event) {
  var obj = JSON.parse(event.data);
  var type = obj.type;
  if (type.localeCompare("plotdata") == 0) { 
    var l_mode = parseInt(obj.value); 
    console.log(l_mode); 
    slider.value = l_mode; 
    output.innerHTML = l_mode;
  }
  else if (type.localeCompare("graph_X") == 0) {
    console.log(obj.value);
	xValues = obj.value.map(x => x * 0.01);
  }  
  else if (type.localeCompare("graph_Y") == 0) {
    console.log(obj.value);
	yValues = obj.value.map(x => x * 0.01);
  }  
  else if (type.localeCompare("graph_Z") == 0) {
    console.log(obj.value);
	zValues = obj.value.map(x => x * 0.01);
	bValues = xValues.map((xi, i) => Math.sqrt(xi**2 + yValues[i]**2 + zValues[i]**2));
	const newLabel = new Date(Date.now()).toString();
	myChart.options.scales.x.title.text = newLabel;
	updatechart();
  }  
}

window.onload = function(event) {
if (0) {
	var d = Date.now();	
	for(var i = 0; i < 300; i++) {
	yValues[i] = i;
	tValues[i] = new Date(d - (300-1-i) * 60 * 1000).toTimeString().slice(0, 5);
	}
	const result = yValues.map(x => x * 0.1);
	const mean = result.reduce((a, b) => a + b, 0) / result.length;
	myChart.data.datasets[0].data = yValues.map(x => 20*Math.sin(5*2*3.1415*x/300));	
	myChart.data.datasets[1].data = result;
	myChart.data.datasets[2].data = result.map(x => x - mean);
	myChart.data.datasets[3].data = myChart.data.datasets[0].data.map((xi, i) => Math.sqrt(xi**2 + myChart.data.datasets[1].data[i]**2 + myChart.data.datasets[2].data[i]**2));	
	console.log(tValues);
	console.log(result);
	console.log(new Date(d).toTimeString());
	myChart.update();
}
init();
}