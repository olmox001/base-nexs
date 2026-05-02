import os
import sys
import requests
import json

# Esempio di script per postare suggerimenti automatici su una PR GitHub
# Richiede GITHUB_TOKEN e i dati della PR dall'environment delle Actions

def post_suggestion(file_path, line, old_text, new_text):
    token = os.getenv("GITHUB_TOKEN")
    repo = os.getenv("GITHUB_REPOSITORY")
    pr_number = os.getenv("PR_NUMBER")
    commit_id = os.getenv("COMMIT_ID")

    if not all([token, repo, pr_number, commit_id]):
        print("Missing environment variables for suggestions")
        return

    url = f"https://api.github.com/repos/{repo}/pulls/{pr_number}/comments"
    headers = {
        "Authorization": f"token {token}",
        "Accept": "application/vnd.github.v3+json"
    }
    
    body = f"Hey! Sembra che tu abbia usato `{old_text}`. In NeXs preferiamo `{new_text}`.\n\n"
    body += f"```suggestion\n{new_text}\n```"

    payload = {
        "body": body,
        "commit_id": commit_id,
        "path": file_path,
        "line": int(line),
        "side": "RIGHT"
    }

    resp = requests.post(url, headers=headers, json=payload)
    if resp.status_code == 201:
        print(f"Suggestion posted to {file_path}:{line}")
    else:
        print(f"Failed to post suggestion: {resp.text}")

if __name__ == "__main__":
    # Questo è solo un trigger di esempio
    # In un caso reale, questo script verrebbe chiamato leggendo l'output del linter
    if len(sys.argv) > 1:
        # Esempio: python nexs-suggest.py example.nx 10 continue cont
        post_suggestion(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4])
