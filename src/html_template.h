#ifndef HTML_TEMPLATE_H
#define HTML_TEMPLATE_H

// This macro converts the HTML file content to a C++ string literal
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

// This will be populated by a build script or manual conversion
// For now, let's embed the HTML directly
static const char HTML_TEMPLATE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html>
<head>
    <title>WiFi Setup</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        body {
            font-family: Arial, sans-serif;
            margin: 20px;
            background: #f5f5f5;
            /* Disable pull-to-refresh */
            overscroll-behavior-y: contain;
            -webkit-overflow-scrolling: touch;
        }

        /* Disable pull-to-refresh more aggressively */
        html, body {
            overflow-x: hidden;
            position: fixed;
            width: 100%;
            height: 100%;
        }

        .container {
            max-width: 400px;
            margin: 0 auto;
            background: white;
            padding: 20px;
            border-radius: 8px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
            overflow-y: auto;
            height: calc(100vh - 40px);
            box-sizing: border-box;
        }
        h1 {
            color: #333;
            text-align: center;
            margin-bottom: 30px;
        }
        .network {
            padding: 10px;
            margin: 5px 0;
            border: 1px solid #ddd;
            border-radius: 4px;
            cursor: pointer;
            background: #f9f9f9;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        .network:hover {
            background: #e9e9e9;
        }
        .network.selected {
            background: #007cba;
            color: white;
        }
        .signal-strength {
            font-size: 12px;
            color: #666;
        }
        .network.selected .signal-strength {
            color: #ccc;
        }
        .lock-icon {
            color: #ff6b6b;
            margin-left: 5px;
        }
        .network.selected .lock-icon {
            color: #ffcccb;
        }
        .form-group {
            margin: 15px 0;
        }
        label {
            display: block;
            margin-bottom: 5px;
            font-weight: bold;
        }
        input[type="text"], input[type="password"] {
            width: 100%;
            padding: 10px;
            border: 1px solid #ddd;
            border-radius: 4px;
            font-size: 16px;
            box-sizing: border-box;
        }
        button {
            width: 100%;
            padding: 12px;
            background: #007cba;
            color: white;
            border: none;
            border-radius: 4px;
            font-size: 16px;
            cursor: pointer;
        }
        button:hover {
            background: #005a8b;
        }
        .refresh {
            background: #28a745;
            margin-bottom: 20px;
        }
        .refresh:hover {
            background: #1e7e34;
        }
        .networks-list {
            max-height: 200px;
            overflow-y: auto;
            border: 1px solid #ddd;
            border-radius: 4px;
            margin-bottom: 15px;
        }
        .no-networks {
            padding: 20px;
            text-align: center;
            color: #666;
            font-style: italic;
        }
        .scanning {
            padding: 20px;
            text-align: center;
            color: #007cba;
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 10px;
        }
        .spinner {
            width: 20px;
            height: 20px;
            border: 2px solid #f3f3f3;
            border-top: 2px solid #007cba;
            border-radius: 50%;
            animation: spin 1s linear infinite;
        }
        @keyframes spin {
            0% { transform: rotate(0deg); }
            100% { transform: rotate(360deg); }
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>WiFi Setup</h1>

        <form action="/" method="GET" style="margin-bottom: 20px;">
            <input type="hidden" name="refresh" value="1">
            <button type="submit" class="refresh">🔄 Refresh Networks</button>
        </form>

        <form action="/connect" method="POST">
            <div class="form-group">
                <label>Available Networks:</label>
                <div class="networks-list">
                    {{NETWORKS_LIST}}
                </div>
            </div>

            <div class="form-group">
                <label for="ssid">Network Name (SSID):</label>
                <input type="text" id="ssid" name="ssid" required>
            </div>

            <div class="form-group">
                <label for="password">Password:</label>
                <input type="password" id="password" name="password">
                <small style="color: #666;">Leave blank for open networks</small>
            </div>

            <button type="submit">Connect</button>
        </form>
    </div>

    <script>
        // Simple click-to-fill functionality
        document.addEventListener('click', function(e) {
            if (e.target.closest('.network')) {
                const networkDiv = e.target.closest('.network');

                // Remove previous selection
                document.querySelectorAll('.network').forEach(n => n.classList.remove('selected'));

                // Select clicked network
                networkDiv.classList.add('selected');

                // Fill SSID field
                const ssid = networkDiv.dataset.ssid;
                if (ssid) {
                    document.getElementById('ssid').value = ssid;

                    // Focus password field if network is secured
                    if (networkDiv.dataset.secured === 'true') {
                        document.getElementById('password').focus();
                    } else {
                        // Clear password for open networks
                        document.getElementById('password').value = '';
                    }
                }
            }
        });

        // Auto-refresh when scanning is detected
        document.addEventListener('DOMContentLoaded', function() {
            function checkForScanningAndRefresh() {
                const scanningElement = document.querySelector('.scanning');
                if (scanningElement) {
                    // If scanning, refresh page in 2 seconds to check for results
                    setTimeout(function() {
                        window.location.reload();
                    }, 2000);
                }
            }

            checkForScanningAndRefresh();
        });

        // Disable pull-to-refresh with JavaScript
        let startY = 0;
        document.addEventListener('touchstart', function(e) {
            startY = e.touches[0].pageY;
        });

        document.addEventListener('touchmove', function(e) {
            const y = e.touches[0].pageY;
            // Disable pull-to-refresh if at top of page and pulling down
            if (y > startY && window.pageYOffset === 0) {
                e.preventDefault();
            }
        }, { passive: false });
    </script>
</body>
</html>)rawliteral";

#endif